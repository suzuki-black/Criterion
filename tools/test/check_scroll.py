#!/usr/bin/env python3
"""M0-1 スクロール検証: openMSX のスクリーンショットを、src/m0/scroll.c と同じ地形定義から
描いた期待画像と 1 ドット単位で照合する。
  usage: check_scroll.py <png> <pos> [<png> <pos> ...]
色は実 RGB ではなく「期待パレット番号 → 画面色」の対応が一貫しているかで判定する
(パレット→RGB 変換の差に依存しないため)。"""
import sys
from PIL import Image

def strip_of(c):
    blk = (c >> 2) & 0xFFFF
    ceil = (blk * 5 + (blk >> 3)) & 3
    floor = (blk * 3 + (blk >> 2) + 1) & 3
    major = 1 if (c & 7) == 0 else 0
    return (major << 4) | (floor << 2) | ceil

def strip_pixel(s, x, y):
    ch = ((s & 3) + 1) * 8; fh = (((s >> 2) & 3) + 1) * 8; major = s >> 4
    top = 13; fy = 212 - fh
    if y < 12: return 15 if (x == 0 and (major or y >= 6)) else 1
    if y == 12: return 14
    if y < top + ch:
        ry = y - top
        if (ry & 7) == 7 or x == (3 if (ry >> 3) & 1 else 7): return 5
        return 4
    if y >= fy:
        ry = y - fy
        if ry == 0: return 10
        if (ry & 7) == 7 or x == (3 if (ry >> 3) & 1 else 7): return 7
        return 6
    if major and x == 0 and (y & 4): return 11
    if x == 3 and y == ((40 + s * 5) & 255): return 2
    return 0

def expected(pos, x, y):
    w = (pos + x) & 0xFFFF
    return strip_pixel(strip_of(w >> 3), w & 7, y)

def check(png, pos):
    img = Image.open(png).convert('RGB')
    W, H = img.size
    # screenshot -raw は 320x240(左右 32・上 14 の枠)。表示 (x,y) は画像 (32+x, 14+y)。
    if (W, H) != (320, 240):
        raise SystemExit(f"{png}: screenshot -raw の 320x240 を想定 ({W}x{H})")
    ox, oy, sc = 32, 14, 1
    m, bad, n = {}, 0, 0
    for y in range(212):
        for x in range(8, 256):
            e = expected(pos, x, y)
            c = img.getpixel((ox + x * sc, oy + y * sc))
            if m.setdefault(e, c) != c: bad += 1
            n += 1
    # 逆対応(異なる番号が同じ色)も不一致とみなす
    inv = {}
    for e, c in m.items():
        if c in inv and e not in (0,) and inv[c] not in (0,):
            bad += 1
        inv.setdefault(c, e)
    return bad, n

if __name__ == '__main__':
    a = sys.argv[1:]
    fail = 0
    for png, pos in zip(a[0::2], a[1::2]):
        bad, n = check(png, int(pos))
        print(f"pos={int(pos):5d}  mismatch {bad}/{n}  {'OK' if bad == 0 else 'NG'}")
        fail |= bad != 0
    sys.exit(1 if fail else 0)
