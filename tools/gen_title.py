#!/usr/bin/env python3
"""gen_title.py — タイトル画面の素材を作る(指示書 4.2)。外部の画像は使わず、手続きで描く。

  usage: gen_title.py <出力ディレクトリ>

1. 夜明けの地球の地平線(256x64、SCREEN12 = GRAPHIC7 + YJK)
   宇宙の暗がりと星、地球の縁(大気の青い光)、右から昇る日の出。
   YJK: 4 画素で J/K を共有。Y は画素ごと(5 ビット)、J = R - Y、K = G - Y(6 ビット符号付き)。
   ★GRAPHIC7 は VRAM を 2 つのバンクに 1 バイトずつ交互に読むので(M0-4)、表示アドレスの偶数バイトと奇数バイトを
     別のファイルにする: title_even.bin → ページ 0 の 0..63 行、title_odd.bin → ページ 2 の 0..63 行。
2. ロゴ「CRITERION」(256x64、SCREEN5 の 4bpp、地は色 1 = 空)→ logo.bin(ページ 2 の 64..127 行)。
   8x8 の文字を横 3.5 倍・縦 8 倍にし、行ごとに金属の色(パレット 11/15/14/9)を変える。
3. 起動ロゴの TDF の紋章(256x48、色 15 だけ)→ tdf.bin(ページ 3 の 140..187 行)。
   円環の目盛りの中に「TDF」、下に「TERRAN DEFENSE FORCE」。
"""
import math, os, random, sys

W, H = 256, 64

def clamp(v, lo, hi): return max(lo, min(hi, v))

def horizon_rgb(x, y):
    """0..31 の RGB。画面上が宇宙、下に地球の縁が弧を描く。"""
    cx, cy, R = 128.0, 64.0 + 420.0, 440.0           # 地球(大きな円)の中心と半径
    d = math.hypot(x - cx, y - cy) - R              # 地表までの距離(負 = 地球の中)
    sun = math.exp(-(((x - 214) / 26.0) ** 2) - ((y - 50) / 9.0) ** 2)
    if d < 0:
        # 地球: 夜の海と雲。日の出の側だけ明るい
        t = clamp(-d / 30.0, 0, 1)
        r, g, b = 2 + 10 * sun, 4 + 8 * sun, 10 - 4 * t
        if (int(x * 0.37 + y * 1.3) % 11) < 2: g += 2; b += 2      # 雲の筋
        return r, g, b
    # 大気の光(縁の外側 0..10)
    glow = math.exp(-d / 4.0)
    r = 3 * glow + 28 * sun * math.exp(-d / 8.0)
    g = 12 * glow + 18 * sun * math.exp(-d / 8.0)
    b = 26 * glow + 8 * sun
    return r, g, b

def yjk_encode(img):
    out = []
    for y in range(H):
        row = []
        for gx in range(0, W, 4):
            px = [img[y][gx + i] for i in range(4)]
            ys = [clamp(int(round(b / 2 + r / 4 + g / 8)), 0, 31) for r, g, b in px]
            ar = sum(p[0] for p in px) / 4; ag = sum(p[1] for p in px) / 4
            ay = sum(ys) / 4
            J = clamp(int(round(ar - ay)), -32, 31) & 63
            K = clamp(int(round(ag - ay)), -32, 31) & 63
            lo = [K & 7, K >> 3, J & 7, J >> 3]
            row += [(ys[i] << 3) | lo[i] for i in range(4)]
        out.append(row)
    return out

FONT = {   # 8x8(大文字だけ)
    'C': [0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00], 'R': [0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00],
    'I': [0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00], 'T': [0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00],
    'E': [0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00], 'O': [0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00],
    'N': [0x63,0x73,0x7B,0x6F,0x67,0x63,0x63,0x00],
    'D': [0x7C,0x66,0x66,0x66,0x66,0x66,0x7C,0x00], 'F': [0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00],
    'A': [0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00], 'S': [0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00],
    ' ': [0, 0, 0, 0, 0, 0, 0, 0],
}
METAL = [15, 15, 11, 11, 14, 14, 9, 9]               # 文字の 8 行それぞれの色(上が明るい)

def logo():
    text = 'CRITERION'
    img = [[1] * W for _ in range(H)]                 # 地は色 1(空の色。帯ごとにラスターで変わる)
    cw = 28                                           # 1 文字の幅(ドット)
    x0 = (W - cw * len(text)) // 2
    for ci, ch in enumerate(text):
        g = FONT[ch]
        for gy in range(8):
            bits = g[gy]
            for sy in range(8):
                y = gy * 8 + sy
                if y >= H: continue
                for gx in range(8):
                    if bits & (0x80 >> gx):
                        for sx in range(3):
                            x = x0 + ci * cw + gx * 3 + sx
                            if 0 <= x < W: img[y][x] = METAL[gy] if sy < 7 else 9
        # 目盛りの意匠: 文字の下に刻み
    for x in range(x0, x0 + cw * len(text), 4):
        img[H - 2][x] = 14
        if (x - x0) % 28 == 0: img[H - 3][x] = 14
    out = bytearray()
    for y in range(H):
        for x in range(0, W, 2):
            out.append((img[y][x] << 4) | img[y][x + 1])
    return bytes(out)

def tdf():
    TH = 48
    img = [[0] * W for _ in range(TH)]
    def put(x, y):
        if 0 <= x < W and 0 <= y < TH: img[y][x] = 15
    cx, cy = 128, 17
    for i in range(360):                               # 円環と目盛り
        a = math.radians(i)
        put(int(round(cx + 17 * math.cos(a))), int(round(cy + 15 * math.sin(a))))
        if i % 30 == 0:
            for r in (18, 19):
                put(int(round(cx + r * math.cos(a))), int(round(cy + r * 0.88 * math.sin(a))))
    for ci, ch in enumerate('TDF'):                    # 中の文字(横 1 倍・縦 2 倍)
        g = FONT[ch]
        for gy in range(8):
            for gx in range(8):
                if g[gy] & (0x80 >> gx):
                    for sy in range(2):
                        put(cx - 12 + ci * 9 + gx, cy - 8 + gy * 2 + sy)
    text = 'TERRAN DEFENSE FORCE'
    x0 = (W - len(text) * 8) // 2
    for ci, ch in enumerate(text):
        g = FONT[ch]
        for gy in range(8):
            for gx in range(8):
                if g[gy] & (0x80 >> gx): put(x0 + ci * 8 + gx, 38 + gy)
    out = bytearray()
    for y in range(TH):
        for x in range(0, W, 2):
            out.append((img[y][x] << 4) | img[y][x + 1])
    return bytes(out)

def main(d):
    rnd = random.Random(7)
    img = [[horizon_rgb(x + 0.5, y + 0.5) for x in range(W)] for y in range(H)]
    for _ in range(70):                                # 星(宇宙の側だけ)
        x, y = rnd.randrange(W), rnd.randrange(40)
        r, g, b = img[y][x]
        if r + g + b < 12: img[y][x] = (24, 24, 28)
    img = [[tuple(clamp(c, 0, 31) for c in p) for p in row] for row in img]
    yjk = yjk_encode(img)
    even = bytes(v for row in yjk for v in row[0::2])
    odd = bytes(v for row in yjk for v in row[1::2])
    open(os.path.join(d, 'title_even.bin'), 'wb').write(even)
    open(os.path.join(d, 'title_odd.bin'), 'wb').write(odd)
    open(os.path.join(d, 'logo.bin'), 'wb').write(logo())
    open(os.path.join(d, 'tdf.bin'), 'wb').write(tdf())

if __name__ == '__main__':
    main(sys.argv[1])
