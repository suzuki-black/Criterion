#!/usr/bin/env python3
"""M0-4 画面分割の検証: run_m0_mode.sh のスクリーンショットを行ごとに照合する。

  usage: check_mode.py <作業ディレクトリ>

分割画面の各行を、上の帯(0..n-1)は「全面 SCREEN12」の同じ行、下の帯(n..211)は「全面 SCREEN5」の同じ行と比べ、
一致しない行(境界で乱れる行)を列挙する。YJK の帯の横位置 x = 1..7 は、x = 0 を x ドットずらしたものと比べる
(4 画素で色を共有する YJK でも、1 ドット単位で単純にずれるだけか)。

合否(終了コード): 乱れる行が境界の直前 3 行(n-3..n-1)に収まり、上下の帯の内側は全行一致。
YJK のずらしは、左端(マスクの隣)の最初の 4 画素の組を除いて全一致(openMSX の実測。実機は要確認)。
"""
import os, re, sys
from PIL import Image

OX, OY = 32, 14   # screenshot -raw(320x240)の表示原点

def rows(p):
    im = Image.open(p).convert('RGB')
    return [[im.getpixel((OX + x, OY + y)) for x in range(8, 256)] for y in range(212)]

def main(d):
    fail = []
    log = {l.split()[0]: dict(re.findall(r'(\w+)=(\d+)', l)) for l in open(os.path.join(d, 'm0_mode.log'))}
    late = sum(int(v.get('late', 0)) for v in log.values())
    print(f"{'設定':22s} 乱れる行")
    for n in (48, 64):
        g4 = rows(os.path.join(d, f'ref_g4_n{n}.png'))
        g7 = rows(os.path.join(d, f'ref_g7_n{n}.png'))
        for order in (0, 1):
            for pcm in (0, 1):
                tag = f'split_n{n}_o{order}_p{pcm}'
                sp = rows(os.path.join(d, tag + '.png'))
                bad = [y for y in range(212) if sp[y] != (g7[y] if y < n else g4[y])]
                inner = [y for y in bad if not (n - 3 <= y <= n - 1)]
                print(f"{tag:22s} {bad if bad else 'なし'}")
                if inner: fail.append(tag)
    base = rows(os.path.join(d, 'yjk_x0.png'))
    for x in range(1, 8):
        r = rows(os.path.join(d, f'yjk_x{x}.png'))
        # 横位置 x: 画面の列 c には元の列 c + x が来る(右へ送る)。帯の 0..47 行、両端の x 列を除いて比べる
        bad = sum(1 for y in range(48) for c in range(4, 248 - x) if r[y][c] != base[y][c + x])
        edge = sum(1 for y in range(48) for c in range(0, 4) if r[y][c] != base[y][c + x])
        print(f"YJK 横位置 {x}: 一致しない画素 {bad}(左端の 4 画素の組 {edge})")
        if bad: fail.append(f'yjk{x}')
    print(f"遅れたフレーム(合計): {late}")
    if late: fail.append('late')
    print('判定: ' + ('OK' if not fail else 'NG ' + ' '.join(fail)))
    return 1 if fail else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
