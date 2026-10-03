#!/usr/bin/env python3
"""check_tempo.py — BGM のもたつきを測る。openMSX で記録した OPLL のキーオンの時刻から、曲ごとに「一定のリズムで
刻み続けるチャンネル」(ベースなど)の間隔と累積のずれを見る。

  usage: check_tempo.py <ログ>

ログ(1 行 1 事象): "<時刻> <レジスタ> <値>"(OPLL のデータ書き込みのうち 0x20..0x25 のキーオンと 0x0E)と、
0.1 秒ごとの "<時刻> bgm <曲> pcm <PCM> [tempo <g_snd_tempo>]"。
  ・1 音の間隔: 本来の間隔(刻み数 / 1 フレームの刻み数)± 2 フレーム(60Hz への丸めで最大 1 フレーム + 遅れたフレームの 1)
  ・累積のずれ: 区間の最初の音からの (実際 − 本来) が ±2 フレーム以内(曲が遅れていく = もたつき)
  ・テンポの演出(tempo が 128 以外。3 面)がかかっている間は数えない。間隔が本来の半分未満の重複(ゆらぎで鳴っている
    音を書き直したもの)は除く。
★全チャンネルを曲データと照合する作りは、タイ・レガート・消音・ループの扱いで照合が崩れ、当てにならなかった。
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import songs, gen_sound

# 曲 → (チャンネル, 1 音の刻み数)。どれも同じ長さの音を休みなく刻むチャンネル
STEADY = {'M03': ('FM4', 6), 'M04': ('FM3', 24), 'M08': ('FM4', 24), 'M10': ('FM0', 6), 'M12': ('FM3', 12),
          'M13': ('FM3', 12), 'M14C': ('FM4', 6), 'M15': ('FM3', 6)}
FPS = 3579545 / 228 / 262                     # ★画面は 1 秒 59.92 フレーム(60 で数えると長い曲で 0.13% ずれて見えた)
TOL_IOI = 2.0                                  # 60Hz への丸め 1 フレーム + 遅れたフレームの 1 フレーム
TOL_DRIFT = 2.0


def main(path):
    bg, ev = [], []
    for l in open(path):
        t = l.split()
        if len(t) < 3:
            continue
        if t[1] == 'bgm':
            tempo = int(t[6]) if len(t) > 6 else 128
            bg.append((float(t[0]), int(t[2]), tempo))
        else:
            ev.append((float(t[0]), int(t[1]), int(t[2])))
    ids = {i: s for i, s in enumerate(songs.SONGS)}
    # 曲の区間(テンポの演出がかかっていない、同じ曲が続く区間)
    spans = []
    for t, sid, tempo in bg:
        key = (sid, tempo == 128)
        if not spans or spans[-1][2] != key:
            if spans:
                spans[-1][1] = t
            spans.append([t, None, key])
    if spans:
        spans[-1][1] = bg[-1][0]
    bad = checked = 0
    for t0, t1, (sid, plain) in spans:
        song = ids.get(sid)
        if not song or not plain or song.id not in STEADY or t1 - t0 < 2.0:
            continue
        chname, ticks = STEADY[song.id]
        ci = gen_sound.CHN.index(chname)
        step = ticks * 256.0 / gen_sound.tempo_rate(song.bpm)          # フレーム
        on = [t for t, a, v in ev if a == 0x20 + ci and t0 + 0.2 <= t < t1 - 0.2]
        on2 = []
        for t in on:
            if not on2 or (t - on2[-1]) * FPS > step * 0.5:
                on2.append(t)
        if len(on2) < 4:
            continue
        ioi = [(b - a) * FPS for a, b in zip(on2, on2[1:])]
        worst_ioi = max(ioi, key=lambda x: abs(x - step))
        n_ioi = sum(1 for x in ioi if abs(x - step) > TOL_IOI)
        drift = [((t - on2[0]) * FPS) - k * step for k, t in enumerate(on2)]
        worst_d = max(drift, key=abs)
        ok = n_ioi == 0 and abs(worst_d) <= TOL_DRIFT
        bad += 0 if ok else 1
        checked += 1
        print(f'{song.id:5s} {t0:7.1f}-{t1:7.1f}s {chname} 音 {len(on2):4d}  間隔 {step:5.2f} フレーム(最大の外れ {worst_ioi:5.2f}、'
              f'{TOL_IOI} を超えた {n_ioi})  累積のずれ {worst_d:+5.2f}  {"OK" if ok else "NG"}')
    print(f'判定: {"OK" if bad == 0 and checked else "NG"} (区間 {checked}、NG {bad})')
    return bad == 0 and checked > 0


if __name__ == '__main__':
    sys.exit(0 if main(sys.argv[1]) else 1)
