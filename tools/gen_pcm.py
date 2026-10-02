#!/usr/bin/env python3
"""gen_pcm.py — PCM の試験音(オーケストラヒット風の長調和音)を合成して C ヘッダに出す。

指示書 6.4: 既存の有名サンプルは使わず、ライセンス上問題のない素材から自作合成する。
ここでは鋸波の和音(D 長調、低音から 3 オクターブ)に、鋭い立ち上がりと指数減衰、
立ち上がりの雑音(打撃感)を加えた。乱数は固定の種で、出力は毎回同じ。

  usage: gen_pcm.py <out.h>
サンプリング周波数は 2 ラインに 1 回 = 15734.26 / 2 Hz(NTSC の水平周波数の半分)。
"""
import math, random, sys

RATE = 15734.264 / 2
DUR = 0.40
NOTES = [73.42, 146.83, 220.00, 293.66, 369.99, 440.00, 587.33]   # D2 D3 A3 D4 F#4 A4 D5

def synth():
    rnd = random.Random(1990)
    n = int(RATE * DUR)
    out = []
    phases = [rnd.random() for _ in NOTES]
    for i in range(n):
        t = i / RATE
        env = min(1.0, t / 0.004) * math.exp(-t / 0.11)
        v = 0.0
        for k, f in enumerate(NOTES):
            ph = (phases[k] + f * 1.003 ** (k % 3) * t) % 1.0
            v += (2 * ph - 1) * (0.9 if f < 300 else 0.6)
        v /= len(NOTES) * 0.55
        v += (rnd.random() * 2 - 1) * 0.5 * math.exp(-t / 0.012)   # 打撃の雑音
        s = max(-1.0, min(1.0, v * env))
        out.append(int(round(128 + s * 127)))
    return out

def main(path):
    data = synth()
    with open(path, 'w') as f:
        f.write('/* pcm_data.h — tools/gen_pcm.py が生成(手で編集しない)。\n')
        f.write(f'   オーケストラヒット風の長調和音 {DUR}s、{RATE:.1f}Hz、8bit 符号なし(無音 = 128)。 */\n')
        f.write('#ifndef PCM_DATA_H\n#define PCM_DATA_H\n\n')
        f.write(f'#define PCM_HIT_LEN {len(data)}u\n')
        f.write('static const u8 pcm_hit[PCM_HIT_LEN] = {\n')
        for i in range(0, len(data), 16):
            f.write('    ' + ', '.join(str(v) for v in data[i:i + 16]) + ',\n')
        f.write('};\n\n#endif /* PCM_DATA_H */\n')

if __name__ == '__main__':
    main(sys.argv[1])
