#!/usr/bin/env python3
"""gen_stage4.py — 4 面「層流」(指示書 5.4)の絵と地図を作る。外部の画像は使わず、手続きで描く。

  usage: gen_stage4.py <出力ディレクトリ> <s4_data.h>

1. s4_strips.bin: 短冊 32 本(8x188、4bpp)= 横 256 ドットの雲の絵 1 枚(短冊 k が x = 8k..8k+7)。
   縦は 12 層の雲の帯(層の境 LAYER_B)。層の頭 2 行は暗い縁(色 6)で、ラスターの切り替えの行を隠す。
   層の中は空(色 1。帯ごとにラスターで色を替える)と、下からわき上がる雲の頂(色 3 影 / 4 / 5 / 7 縁の光)。
   横は周期 256 なので、表示リング(512 幅)のどこを見せても絵がつながる(帯ごとに横位置を自由に変えられる)。
   ★0..3 行は必ず色 6(stage.c のブロック 23)。
2. s4_map.bin: 地図 640 列(短冊 0..31 のくり返し)。
3. face.bin: STRATUM の顔(128x144、4bpp。ページ 2 の 0..143 行・0..127 ドットへ読む)。6 本の帯(24 行ずつ)
   の境は暗い縁。外は色 1(空)。
4. s4_parts.bin: RULER の体節と VERNIER の板(128x64。ページ 2 の x = 128..255、0..63 行へ)。
5. s4_data.h: 層の境の行。
"""
import math, os, random, sys

W, H = 256, 188
NL = 12
LAYER_B = [round(H * k / NL) for k in range(NL + 1)]          # 0, 16, 31, ... 188
img = [[1] * W for _ in range(H)]

def put(x, y, c):
    if 0 <= x < W and 0 <= y < H: img[y][x] = c

def profile(seed, x):
    """周期 256 の雲の頂の高さ(0..1)"""
    r = random.Random(seed)
    terms = [(k, r.uniform(0, 6.283), r.uniform(0.3, 1.0) / k) for k in (1, 2, 3, 5, 8, 13)]
    v = sum(a * math.sin(2 * math.pi * k * x / W + p) for k, p, a in terms)
    base = 0.45 + 0.3 * v
    for i in range(7):                                        # もこもこ(周期 256 で並べた丸い頂)
        c = (i * 37 + r.randrange(30)) % W
        rad = r.uniform(8, 22)
        d = min(abs(x - c), W - abs(x - c))
        if d < rad: base = max(base, 0.35 + 0.55 * math.sqrt(1 - (d / rad) ** 2) * r.uniform(0.7, 1.0))
    return base

for L in range(NL):
    y0, y1 = LAYER_B[L], LAYER_B[L + 1]
    edge = 4 if L == 0 else 2
    for y in range(y0, y0 + edge):
        for x in range(W): put(x, y, 6)
    hgt = y1 - y0 - edge
    for x in range(W):
        top = y1 - int(hgt * min(0.95, 0.1 + 0.85 * profile(400 + L, x)))   # 雲の頂の行
        for y in range(max(top, y0 + edge), y1):
            d = y - top
            c = 7 if d == 0 else 5 if d < 2 else 4 if d < 5 else 3
            if c == 4 and (x * 7 + y * 3 + L) % 11 == 0: c = 5             # 雲のきめ
            if c == 3 and (x + y * 5 + L) % 9 == 0: c = 4
            put(x, y, c)

def face():
    """STRATUM の顔(128x144)。層になった石の要塞: 額・目・鼻梁・口。帯 6 本の境は色 6 の縁"""
    N, M = 128, 144
    s = [[1] * N for _ in range(M)]
    cx = 63.5
    for y in range(M):
        hw = 60 - max(0, abs(y - 72) - 50) * 1.2                     # 角を丸めた輪郭
        for x in range(N):
            if abs(x - cx) > hw: continue
            band = y // 24
            c = (3, 4, 5, 4, 3, 4)[band]
            if (y % 24) in (0, 1): c = 6                               # 帯の境(ラスターの切り替えを隠す)
            elif (x // 6 + y // 3) % 7 == 0: c = 3                     # 層の筋
            if abs(x - cx) > hw - 2: c = 7                             # 縁の光
            s[y][x] = c
    def ellipse(ex, ey, rx, ry, col, inner=None):
        for y in range(M):
            for x in range(N):
                d = ((x - ex) / rx) ** 2 + ((y - ey) / ry) ** 2
                if d <= 1: s[y][x] = inner if inner is not None and d < 0.35 else col
    ellipse(38, 40, 14, 8, 2, 13)                                      # 目(奥は赤い灯)
    ellipse(89, 40, 14, 8, 2, 13)
    for y in range(52, 92):                                            # 鼻梁
        for x in range(58, 70): s[y][x] = 5 if x < 62 else 3
    for y in range(104, 116):                                          # 口(格子)
        for x in range(24, 104):
            s[y][x] = 2 if (x // 6) % 2 else 13 if y in (109, 110) else 2
    out = bytearray()
    for y in range(M):
        for x in range(0, N, 2): out.append((s[y][x] << 4) | s[y][x + 1])
    return bytes(out)

def parts():
    """ページ 2 の x = 128..255 に置く部品(128x64)。1 つを HMMM 1 回で写す(命令 1 つで CPU 約 0.3ms)。
       (0, 0) RULER の体節 28x12 / (32, 0) その頭 / 行 24..43 VERNIER の上の板(224x10 を左右 112 ずつ)/
       行 44..63 下の板。上の目盛りは 8 + 16i、下は 8 + 15j(s4boss.c の vern_match と合わせる)"""
    N, M = 128, 64
    s = [[0] * N for _ in range(M)]
    for ox, head in ((0, False), (32, True)):
        for y in range(12):
            for x in range(28):
                c = 15 if y == 0 else 2 if y == 11 else 14
                if x in (7, 14, 21) and 1 <= y < (7 if x == 14 else 5): c = 2      # 目盛り
                if head and 3 <= x < 7 and 4 <= y < 8: c = 13                       # 目
                s[y][ox + x] = c
    def plate(row0, top):
        for y in range(10):
            for x in range(224):
                c = 9
                if (top and y == 9) or (not top and y == 0): c = 14
                tick = (x - 8) % 16 == 0 or (x - 8) % 16 == 1 if top else ((x - 8) % 15 in (0, 1) and x >= 8)
                if top and x < 8: tick = False
                if tick and ((top and y >= 5) or (not top and y < 5)): c = 15
                hx = x if x < 112 else x - 112
                s[row0 + y + (0 if x < 112 else 10)][hx] = c
    plate(24, True)
    plate(44, False)
    out = bytearray()
    for y in range(M):
        for x in range(0, N, 2): out.append((s[y][x] << 4) | s[y][x + 1])
    return bytes(out)

def main(d, hpath):
    out = bytearray()
    for y in range(H):
        for x in range(0, W, 2): out.append((img[y][x] << 4) | img[y][x + 1])
    open(os.path.join(d, 's4_strips.bin'), 'wb').write(bytes(out))
    open(os.path.join(d, 's4_map.bin'), 'wb').write(bytes(i & 31 for i in range(640)))
    open(os.path.join(d, 'face.bin'), 'wb').write(face())
    open(os.path.join(d, 's4_parts.bin'), 'wb').write(parts())
    with open(hpath, 'w') as f:
        f.write('/* s4_data.h — tools/gen_stage4.py が生成(手で編集しない)。4 面の雲の層。 */\n')
        f.write('#ifndef S4_DATA_H\n#define S4_DATA_H\n\n')
        f.write(f'#define S4_COLS 640\n#define S4_NL {NL}\n')
        f.write('static const u8 s4_layer[S4_NL + 1] = { ' + ', '.join(str(b) for b in LAYER_B) + ' };   /* 層の境の行(s) */\n')
        f.write('\n#endif /* S4_DATA_H */\n')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
