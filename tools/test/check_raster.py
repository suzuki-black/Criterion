#!/usr/bin/env python3
"""M0-2 ラスター検証: run_m0_raster.sh が撮った(時間を止めた)スクリーンショットを、
src/m0/raster.c と同じ定義から求めた期待値と照合する。

  usage: check_raster.py <作業ディレクトリ>   (m0_raster.log と m*_h*_b*.png を読む)

判定項目(1 枚ごと):
  境界   帯 b の目印色が最初に現れる行 - 期待行(16 + b*148/n)。全帯で 0 なら OK
  横位置 各行の目印の位相(32 ドット周期)が、その行に見えている帯の期待スクロールと一致するか
  陽炎   164..211 行の各行の位相が、正弦波表と一致するか(表の当たり始めのずれ k も求める)

合否(終了コード): 直接方式(m1)の全設定で、境界のずれが ±1 行以内・陽炎 R#27 のみ(h2)が 46/48 行以上。
BIOS 方式(m0)と陽炎 R#26+R#27(h1)は参考値(既知の制約。docs/M0_技術検証.md)。
"""
import re, sys, os
from PIL import Image

LEV = [0, 39, 82, 115, 155, 186, 224, 255]          # openMSX の 3bit → 8bit
def rgb(r, g, b): return (LEV[r], LEV[g], LEV[b])

BAND_RGB = [(7,0,0),(0,7,0),(0,0,7),(7,4,0),(4,0,7),(0,4,7),(7,0,7),(4,7,0),
            (7,4,4),(4,4,7),(4,7,4),(7,2,5),(2,5,7),(5,7,2),(6,3,0),(0,3,6)]
BAND_COL = [rgb(*c) for c in BAND_RGB]
HUD_COL = rgb(7, 7, 7)
HAZE_COL = rgb(0, 7, 7)
SINE = [0, 1, 2, 3, 5, 6, 7, 8, 8, 9, 10, 11, 11, 11, 12, 12, 12, 12, 12, 11, 11, 11, 10, 9, 8, 8, 7, 6, 5, 3, 2, 1,
        0, -1, -2, -3, -5, -6, -7, -8, -8, -9, -10, -11, -11, -11, -12, -12, -12, -12, -12, -11, -11, -11, -10, -9,
        -8, -8, -7, -6, -5, -3, -2, -1]
TOP, AREA, HAZE_Y, HAZE_N = 16, 148, 164, 48
OX, OY = 32, 14                                      # screenshot -raw(320x240)の表示原点

def speed(b):
    v = 1 + (b & 3)
    return -v if b & 1 else v

def band_line(b, n): return TOP + b * AREA // n

def row(img, y):
    return [img.getpixel((OX + x, OY + y)) for x in range(256)]

def phase(px, col):
    """行の画素から、目印(col)の位相 s(= スクロール量 mod 32)を推定。目印は (x+s)&31 < 4。"""
    marks = [x for x in range(8, 256) if px[x] == col]
    if not marks: return None
    best, bs = -1, None
    for s in range(32):
        ok = sum(1 for x in range(8, 256) if (((x + s) & 31) < 4) == (px[x] == col))
        if ok > best: best, bs = ok, s
    return bs if best == 248 else None

def check(png, n, t, haze):
    """haze: 0=無し 1=R#26+R#27(振幅±12) 2=R#27のみ(振幅±3)"""
    img = Image.open(png).convert('RGB')
    res = {}
    # 境界: 各行で見えている帯
    seen = {}
    for y in range(TOP, HAZE_Y):
        px = row(img, y)
        for b in range(n):
            if BAND_COL[b] in px:
                seen.setdefault(b, y)
                break
    res['offsets'] = [seen.get(b, None) if seen.get(b) is None else seen[b] - band_line(b, n) for b in range(n)]
    # 横位置
    bad_rows = 0
    for y in range(TOP, HAZE_Y):
        px = row(img, y)
        b = next((b for b in range(n) if BAND_COL[b] in px), None)
        if b is None: bad_rows += 1; continue
        s = phase(px, BAND_COL[b])
        if s is None or s != (t * speed(b)) % 32: bad_rows += 1
    res['scroll_bad_rows'] = bad_rows
    # 陽炎
    if haze:
        obs = []
        for y in range(HAZE_Y, 212):
            obs.append(phase(row(img, y), HAZE_COL))
        best = None
        for k in range(-3, 4):
            ok = 0
            for i, s in enumerate(obs):
                j = i - k
                if s is None or not (0 <= j < HAZE_N): continue
                w = SINE[(j * 4 + t * 2) & 63]
                x = ((t & ~7) + 4 + int(w / 4)) if haze == 2 else (t + w)
                if s == x % 32: ok += 1
            if best is None or ok > best[1]: best = (k, ok)
        res['haze_k'], res['haze_ok'] = best
    return res

def main(d):
    fail = []
    log = open(os.path.join(d, 'm0_raster.log')).read().splitlines()
    shot_t = {l.split()[1]: int(l.split('t=')[1]) for l in log if l.startswith('SHOT ')}
    log = [l for l in log if not l.startswith('SHOT ')]
    print(f"{'設定':12s} {'残量%':>5s} {'遅れ':>4s}  境界のずれ(帯ごと, 行)                 横位置NG行  陽炎(k, 一致行/48)")
    for line in log:
        tag = line.split()[0]
        kv = dict(re.findall(r'(\w+)=(\d+)', line))
        m, h, n = re.match(r'm(\d)_h(\d)_b(\d+)', tag).groups()
        png = os.path.join(d, tag + '.png')
        if not os.path.exists(png): continue
        r = check(png, int(n), shot_t[tag], int(h))
        offs = ' '.join('-' if o is None else f'{o:+d}' for o in r['offsets'])
        hz = f"k={r['haze_k']:+d} {r['haze_ok']:2d}/48" if h != '0' else ''
        print(f"{tag:12s} {kv['pct']:>5s} {kv['late']:>4s}  {offs:38s} {r['scroll_bad_rows']:5d}      {hz}")
        if m == '1':
            ok = all(o is not None and abs(o) <= 1 for o in r['offsets']) and kv['late'] == '0'
            if h == '2': ok = ok and r['haze_ok'] >= 46
            if h != '1' and not ok: fail.append(tag)
    print('判定: ' + ('OK(直接方式の全設定)' if not fail else 'NG ' + ' '.join(fail)))
    return 1 if fail else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
