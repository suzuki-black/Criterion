#!/usr/bin/env python3
"""snd_pcm.py — PCM の素材(指示書 6.4)をすべて手続きで合成する。外部の音源・既存のサンプルは使わない。

  オーケストラヒット(長調 / 短調 / 低音の減和音)、逆再生シンバル、スネア、大爆発、音声 6 つ。
  音声は自作の簡単なフォルマント合成(声帯のパルス列 / 雑音 → 3 つの共振器)。音素ごとのフォルマントを
  補間し、最後にビットを落として機械の声にする。
  サンプリング周波数は刻み方式の 2 ラインに 1 回(15734.26 / 2 Hz)、8 ビット符号なし(無音 = 128)。
  乱数は固定の種で、出力は毎回同じ。gen_sound.py から build() を呼ぶ。
"""
import math, os, random, wave

RATE = 3579545 / 14 / 32               # 出力: システムタイマ 32 カウントごと(7.99kHz)。方式 B(snd.c の pcmx)の間隔と合わせる
OS = 8
RH = RATE * OS                        # 合成はこの 8 倍で行い、低域通過をかけて間引く(低い周波数で直接作ると折り返し雑音が出る)


def decimate(sig):
    """窓つき sinc の低域通過(出力のナイキストの 9 割)をかけて OS 分の 1 に間引く"""
    taps = 8 * OS + 1
    fc = 0.45 / OS
    h = []
    for k in range(taps):
        m = k - taps // 2
        v = 2 * fc if m == 0 else math.sin(2 * math.pi * fc * m) / (math.pi * m)
        w = 0.42 - 0.5 * math.cos(2 * math.pi * k / (taps - 1)) + 0.08 * math.cos(4 * math.pi * k / (taps - 1))
        h.append(v * w)
    out = []
    n = len(sig)
    for i in range(0, n, OS):
        acc = 0.0
        for k in range(taps):
            j = i + k - taps // 2
            if 0 <= j < n:
                acc += sig[j] * h[k]
        out.append(acc)
    return out


def to_u8(sig, gain=1.0, bits=8, drive=0.0):
    """drive > 0: ソフトクリップ(tanh)で平均の音量を上げる(★そのままでは曲に埋もれて聞こえなかった。M7)"""
    peak = max(1e-9, max(abs(v) for v in sig))
    q = 1 << (8 - bits)
    out = []
    for v in sig:
        s = v / peak * gain
        if drive > 0:
            s = math.tanh(drive * s) / math.tanh(drive)
        s = max(-1.0, min(1.0, s))
        b = int(round(128 + s * 127))
        b = (b // q) * q + (q // 2 if q > 1 else 0)
        out.append(max(0, min(255, b)))
    # 終わりを無音(128)へ寄せる(刻み方式は最後の値を出し続ける)
    for i in range(min(64, len(out))):
        k = len(out) - 1 - i
        out[k] = int(round(128 + (out[k] - 128) * i / 64))
    return out


def orch(notes, dur, tau, seed):
    rnd = random.Random(seed)
    n = int(RH * dur)
    ph = [rnd.random() for _ in notes]
    out = []
    for i in range(n):
        t = i / RH
        env = min(1.0, t / 0.004) * math.exp(-t / tau)
        v = 0.0
        for k, f in enumerate(notes):
            p = (ph[k] + f * (1.0 + 0.003 * (k % 3)) * t) % 1.0
            v += (2 * p - 1) * (0.9 if f < 300 else 0.6)
        v /= len(notes) * 0.55
        v += (rnd.random() * 2 - 1) * 0.5 * math.exp(-t / 0.012)
        out.append(v * env)
    return out


def noise_lp(n, seed, a):
    rnd = random.Random(seed)
    y, out = 0.0, []
    for _ in range(n):
        y += a * ((rnd.random() * 2 - 1) - y)
        out.append(y)
    return out


def cymbal_rev(dur):
    rnd = random.Random(77)
    n = int(RH * dur)
    fr = [540, 800, 1230, 1660, 2210, 2970, 3400]
    out = []
    for i in range(n):
        t = i / RH
        v = sum(1.0 if ((f * t) % 1.0) < 0.5 else -1.0 for f in fr) / len(fr)
        v = v * 0.5 + (rnd.random() * 2 - 1) * 0.8
        env = math.exp(-(dur - t) / 0.28) * min(1.0, (dur - t) / 0.01)
        out.append(v * env)
    return out


def snare(dur):
    rnd = random.Random(55)
    n = int(RH * dur)
    out = []
    for i in range(n):
        t = i / RH
        v = (rnd.random() * 2 - 1) * math.exp(-t / 0.05) + math.sin(2 * math.pi * 185 * t) * math.exp(-t / 0.03)
        out.append(v)
    return out


def explosion(dur):
    n = int(RH * dur)
    lo = noise_lp(n, 99, 0.08)
    hi = noise_lp(n, 98, 0.5)
    out = []
    for i in range(n):
        t = i / RH
        env = min(1.0, t / 0.005) * math.exp(-t / 0.22)
        out.append((lo[i] * 3.0 + hi[i] * math.exp(-t / 0.05)) * env)
    return out


# ---- 音声: 音素 = (長さ ms, 有声の強さ, 雑音の強さ, (F1, F2, F3) or None, 雑音の中心) ----
V = {
    'IH': (400, 1900, 2550), 'IY': (290, 2250, 3000), 'EH': (530, 1840, 2480), 'AE': (660, 1720, 2410),
    'AA': (730, 1090, 2440), 'AO': (570, 840, 2410), 'AH': (640, 1190, 2390), 'UH': (440, 1020, 2240),
    'ER': (490, 1350, 1690), 'OW': (500, 900, 2300), 'EY': (450, 2000, 2600), 'AY': (680, 1300, 2500),
    'N': (280, 1700, 2500), 'M': (280, 1000, 2200), 'NG': (280, 2300, 2750), 'L': (360, 1300, 2700),
    'R': (420, 1250, 1600), 'W': (300, 650, 2200), 'V': (300, 1100, 2300), 'ZH': (300, 1800, 2500),
    'B': (250, 900, 2200), 'G': (280, 1800, 2400),
}
PH = {   # 長さ, 有声, 雑音, 雑音の中心(Hz)
    'IH': (85, 1.0, 0, 0), 'IY': (110, 1.0, 0, 0), 'EH': (110, 1.0, 0, 0), 'AE': (120, 1.0, 0, 0),
    'AA': (130, 1.0, 0, 0), 'AO': (130, 1.0, 0, 0), 'AH': (90, 1.0, 0, 0), 'UH': (90, 1.0, 0, 0),
    'ER': (120, 1.0, 0, 0), 'OW': (150, 1.0, 0, 0), 'EY': (140, 1.0, 0, 0), 'AY': (170, 1.0, 0, 0),
    'N': (65, 0.5, 0, 0), 'M': (70, 0.5, 0, 0), 'NG': (80, 0.5, 0, 0), 'L': (60, 0.7, 0, 0),
    'R': (60, 0.7, 0, 0), 'W': (55, 0.6, 0, 0), 'V': (60, 0.4, 0.3, 3000), 'ZH': (80, 0.4, 0.5, 2500),
    'B': (30, 0.3, 0.1, 1000), 'G': (40, 0.2, 0.5, 1800),
    'S': (110, 0, 1.0, 3500), 'SH': (120, 0, 1.0, 2500), 'F': (90, 0, 0.5, 3200),
    'T': (55, 0, 0.8, 3200), 'K': (60, 0, 0.8, 2000), 'CH': (120, 0, 1.0, 2600),
    '_': (90, 0, 0, 0), '.': (40, 0, 0, 0),
}


def resonator(f, bw):
    r = math.exp(-math.pi * bw / RH)
    c1 = 2 * r * math.cos(2 * math.pi * f / RH)
    c2 = -r * r
    return c1, c2, 1 - c1 - c2


def voice(phs, f0=105.0, f1=None, speed=1.0, seed=1):
    rnd = random.Random(seed)
    seg = []
    for p in phs:
        d, vo, no, nc = PH[p]
        seg.append((p, int(RH * d / 1000 * speed), vo, no, nc))
    total = sum(s[1] for s in seg)
    f1 = f1 if f1 is not None else f0 * 0.8
    out = []
    y = [[0.0, 0.0] for _ in range(3)]
    yn = [0.0, 0.0]
    phase = 0.0
    pos = 0
    prev_fm = (500, 1500, 2500)
    for si, (p, n, vo, no, nc) in enumerate(seg):
        fm = V.get(p, prev_fm)
        nxt = V.get(seg[si + 1][0], fm) if si + 1 < len(seg) else fm
        for i in range(n):
            t = pos / max(1, total)
            f0t = f0 + (f1 - f0) * t
            a = i / max(1, n)
            # 前後の音素へ滑らかに(頭 25% は前から、終わり 25% は次へ)
            if a < 0.25:
                w = 0.5 + 2 * a
                cur = [prev_fm[k] * (1 - w) + fm[k] * w for k in range(3)]
            elif a > 0.75:
                w = (a - 0.75) * 2
                cur = [fm[k] * (1 - w) + nxt[k] * w for k in range(3)]
            else:
                cur = list(fm)
            phase += f0t / RH
            src = 0.0
            if phase >= 1.0:
                phase -= 1.0
                src = 1.0
            src = src * vo
            v = src
            for k, (f, bw) in enumerate(zip(cur, (70, 100, 160))):
                c1, c2, g = resonator(min(f, RH / 2 - 200), bw)
                yk = g * v + c1 * y[k][0] + c2 * y[k][1]
                y[k][1] = y[k][0]
                y[k][0] = yk
                v = yk
            if no:
                c1, c2, g = resonator(min(nc, RH / 2 - 250), 900)
                x = rnd.random() * 2 - 1
                z = g * x + c1 * yn[0] + c2 * yn[1]
                yn[1] = yn[0]
                yn[0] = z
                env = 1.0
                if p in ('T', 'K', 'G', 'B'):
                    env = math.exp(-a * 5) if a > 0.4 else (0 if a < 0.4 else 1)
                v += z * no * 0.35 * env
            out.append(v)
            pos += 1
        prev_fm = fm
    return out


# ---- 外部の素材(assets/pcm、どれも CC0。出典は assets/pcm/LICENSE.md) ----
ASSETS = os.path.join(os.path.dirname(__file__), '..', 'assets', 'pcm')


def load_wav(name):
    """WAV(16 / 24 ビット、モノラル / ステレオ)を -1..1 のモノラルで読む。(標本列, 周波数)"""
    w = wave.open(os.path.join(ASSETS, name))
    ch, sw, fr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
    out = []
    step = sw * ch
    full = float(1 << (8 * sw - 1))
    for i in range(0, len(raw) - step + 1, step):
        acc = 0
        for c in range(ch):
            b = raw[i + c * sw:i + (c + 1) * sw]
            v = int.from_bytes(b, 'little', signed=True)
            acc += v
        out.append(acc / ch / full)
    return out, fr


def resample(sig, src, dst, pitch=1.0):
    """窓つき sinc の低域通過で src → dst へ(pitch > 1 で高く、< 1 で低く。長さも変わる)"""
    ratio = src * pitch / dst                   # 出力 1 標本あたりに進む入力の標本数
    fc = 0.45 * min(1.0, 1.0 / ratio)           # 入力の標本で正規化した遮断(出力のナイキストの 9 割)
    half = max(8, int(4 / fc))
    out = []
    n = len(sig)
    pos = 0.0
    while pos < n:
        c = int(pos)
        acc = 0.0
        for j in range(c - half, c + half + 1):
            if 0 <= j < n:
                x = j - pos
                v = 2 * fc if x == 0 else math.sin(2 * math.pi * fc * x) / (math.pi * x)
                w = 0.42 + 0.5 * math.cos(math.pi * x / (half + 1)) + 0.08 * math.cos(2 * math.pi * x / (half + 1))
                acc += sig[j] * v * w
        out.append(acc)
        pos += ratio
    return out


def clip(sig, t0, t1, fade=0.05):
    """t0..t1 秒(出力の周波数)を切り出し、終わり fade 秒をフェードアウト"""
    a, b = int(t0 * RATE), int(t1 * RATE)
    seg = sig[a:b]
    nf = int(fade * RATE)
    for i in range(min(nf, len(seg))):
        seg[len(seg) - 1 - i] *= i / nf
    return seg


def ext(name, t0, t1, pitch=1.0, fade=0.05):
    sig, fr = load_wav(name)
    head = sig[:int((t1 / pitch + 0.1) * fr) + 1] if t0 == 0 else sig
    return clip(resample(head, fr, RATE, pitch), t0, t1, fade)


OH = '90741__bigdumbweirdo__orch-hit.wav'
SN = '82249__kevoy__acoustic-snare.wav'
RC = '23127__john-rayson__reverse-cymbal.wav'
EX = '587886__samsterbirdies__explosion-large.wav'


def build():
    """[(名前, 優先度, バイト列)]。優先度: 音声 4 > オケヒ 3 > 爆発 2 > スネア 1"""
    res = []
    res.append(('P01', 3, to_u8(ext(OH, 0, 0.6, fade=0.15))))                  # オケヒ(CC0 の素材)
    res.append(('P02', 3, to_u8(ext(OH, 0, 0.6, pitch=0.8409, fade=0.15))))   # 短 3 度下(素材は 1 つなので音程で使い分ける)
    res.append(('P03', 3, to_u8(ext(OH, 0, 0.8, pitch=0.5, fade=0.25))))      # 1 オクターブ下の低い一発
    res.append(('P04', 2, to_u8(ext(RC, 0, 0.45, fade=0.1))))                  # 逆再生シンバル(0.3 秒で頂点)
    res.append(('P05', 1, to_u8(ext(SN, 0, 0.25, fade=0.08))))                 # スネア
    res.append(('P06', 2, to_u8(ext(EX, 0, 1.0, fade=0.3))))                  # 大爆発
    res.append(('V01', 4, to_u8(decimate(voice('IH N F IH N IH T AH S _ L AO N CH'.split(), 112, 96, 1.05, 11)), 1.0, 6)))
    res.append(('V02', 4, to_u8(decimate(voice('W AO R N IH NG'.split(), 118, 104, 1.0, 12)), 1.0, 6)))
    res.append(('V03', 4, to_u8(decimate(voice('S IH S T AH M _ AA N L AY N'.split(), 108, 100, 1.0, 13)), 1.0, 6)))
    res.append(('V04', 4, to_u8(decimate(voice('AH N M EH ZH ER AH B AH L'.split(), 78, 66, 1.15, 14)), 1.0, 5)))
    res.append(('V05', 4, to_u8(decimate(voice('G EY M _ OW V ER'.split(), 96, 80, 1.1, 15)), 1.0, 6)))
    res.append(('V06', 4, to_u8(decimate(voice('K AE L IH B R EY SH AH N'.split(), 110, 100, 1.0, 16)), 1.0, 6)))
    return res


if __name__ == '__main__':
    import sys, wave
    d = sys.argv[1] if len(sys.argv) > 1 else '.'
    for name, prio, data in build():
        w = wave.open(f'{d}/{name}.wav', 'wb')
        w.setnchannels(1); w.setsampwidth(1); w.setframerate(int(RATE))
        w.writeframes(bytes(data)); w.close()
        print(name, len(data))
