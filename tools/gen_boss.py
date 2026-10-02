#!/usr/bin/env python3
"""gen_boss.py — 1 面のボス PLUMB の下げ振り(64x64、4bpp)と、ROTOR の答え合わせ用の絵を作る。

  usage: gen_boss.py <出力ディレクトリ>

1. bob.bin(2048 バイト、1 行 32 バイト): 真鍮の下げ振り。上に吊り輪(弱点)、下へ尖る。外は色 1(空)。
   胴は経線の縞を色 2..5 で塗る。ゲームは色 2..5 のパレットを回して自転して見せる(指示書 5.1「16 階調の
   金属陰影をパレットで表現」の簡略版: 都市の区間では色 2..5 は使っていない)。縁は色 6、光の筋は色 7、
   緯線の目盛りは色 12、吊り輪は 15 / 13。
2. bob_ref.bin: src/core/rotor.c と同じ整数計算で作った、ゲームが作るはずの 10 枚(boss.c の PLUMB_FRAMES と
   同じ順)。tools/test/run_game_m3.sh が VRAM と 1 バイトずつ照合する。
"""
import math, os, sys

N = 64
G_SIN64 = [0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
           127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
           0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
           -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12]
# boss.c の PLUMB_FRAMES と同じ: (角度 0..63、step 8.8)
FRAMES = [(-6, 256), (-4, 256), (-2, 256), (0, 256), (2, 256), (4, 256), (6, 256),
          (0, 301), (0, 366), (0, 465)]

def bob():
    img = [[1] * N for _ in range(N)]
    cx = 32
    # 吊り輪
    for y in range(N):
        for x in range(N):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - 7)
            if 3.0 <= d < 6.2: img[y][x] = 15 if (x < cx) else 13
    # 首
    for y in range(12, 16):
        for x in range(cx - 3, cx + 3): img[y][x] = 12
    # 胴(上は丸く膨らみ、下は円錐で尖る)
    for y in range(15, 63):
        if y <= 30: r = 6 + 16 * math.sin((y - 15) / 15 * math.pi / 2)
        else: r = 22 * (62 - y) / 32
        if r < 0.6: continue
        for x in range(N):
            t = (x + 0.5 - cx) / r
            if abs(t) > 1: continue
            if abs(t) > 0.9: c = 6                          # 縁
            elif -0.62 < t < -0.42: c = 7                    # 光の筋(左上から)
            elif y % 6 == 2 and 18 <= y <= 56: c = 12        # 緯線の目盛り
            else:
                phi = math.asin(t)
                c = 2 + int((phi + math.pi / 2) / math.pi * 10) % 4   # 経線の縞
            img[y][x] = c
    return img

def pack(img, n):
    out = bytearray()
    for y in range(n):
        for x in range(0, n, 2): out.append((img[y][x] << 4) | img[y][x + 1])
    return bytes(out)

def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000

def rotor(src, n, angle, step, bg):
    """src/core/rotor.c と同じ計算(s16 の桁あふれも同じ)"""
    a = angle & 0xFF
    c = G_SIN64[(a + 16) & 63]; s = G_SIN64[a & 63]
    mul7 = lambda k: s16((k * step) >> 7)
    dux, dvx = mul7(c), s16(-mul7(s))
    duy, dvy = mul7(s), mul7(c)
    h = n >> 1
    u = s16(0x2080 - dux * h - duy * h); v = s16(0x2080 - dvx * h - dvy * h)
    out = [[bg] * n for _ in range(n)]
    for y in range(n):
        uu, vv = u, v
        for x in range(n):
            ui = (uu >> 8) & 0xFF; vi = (vv >> 8) & 0xFF
            out[y][x] = src[vi][ui] if ui < 64 and vi < 64 else bg
            uu = s16(uu + dux); vv = s16(vv + dvx)
        u = s16(u + duy); v = s16(v + dvy)
    return out

def main(d):
    img = bob()
    open(os.path.join(d, 'bob.bin'), 'wb').write(pack(img, N))
    ref = bytearray()
    for a, st in FRAMES: ref += pack(rotor(img, N, a, st, 1), N)
    open(os.path.join(d, 'bob_ref.bin'), 'wb').write(bytes(ref))

if __name__ == '__main__':
    main(sys.argv[1])
