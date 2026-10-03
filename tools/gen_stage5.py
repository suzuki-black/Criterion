#!/usr/bin/env python3
"""gen_stage5.py — 5 面「規格戦艦ノルム」(指示書 5.5)の絵と地図を作る。外部の画像は使わず、手続きで描く。

  usage: gen_stage5.py <出力ディレクトリ> <s5_data.h>

縦の割り付け(短冊の行 = 世界の s): 甲板 40、艦体 40..95、艦底 96..99、雲海 112..187。
  甲板の上を飛ぶ区間はカメラ s = -60(上 60 行は高い空)、艦底の区間はカメラ s = 0(上半分が艦底、下半分が雲海)。
  ★0..3 行は色 6(stage.c のブロック 23)。5 面は色 6 を空の色にして見えなくする(艦の暗い色は 2)。
1. s5_strips.bin: 短冊 32 本(8x188、4bpp)
     0..7 空と雲海(周期 64 ドット。接近の間は世界を止めて帯で流すので、64 の約数でくり返す)
     8..10 艦首 / 11, 12 甲板 / 13 機銃の台 / 14..17 主砲塔の台(32 ドット)/ 18, 19 格納庫の扉 /
     20 格納庫の中 / 21 誘爆の穴 / 22 艦底の吊り砲台の台 / 23 装甲の破れ目 / 24 機関室 / 25, 26 プレスの歯と溝 /
     27 出口の縦穴 / 28..30 艦橋の土台 / 31 艦尾
2. s5_map.bin: 地図 640 列(接近 96 → 艦首 96 → 甲板 160 → 格納庫 40 → 艦底 96 → 機関部 96 → 艦橋 40 → 艦尾 16)
3. s5_data.h: 区間の列、短冊ごとの当たり(種類・上・下)、機銃・主砲塔・吊り砲台の列
4. turret.bin: 主砲塔の上部 32x32 × 16 向き(左 → 真上 → 右の半円。ページ 2 の 0..63 行へ、8 枚ずつ 2 段)
5. far.bin: 遠くの戦艦 8 段(ページ 2 の 64..127 行へ)。表は far_rect
6. s5_spr.bin: スプライト 11 枚(機銃 8 向き = 半円、吊り砲台 3 向き)
7. gauge.bin: GAUGE BLOCK のブロック 6 個(長さ 32..112、高さ 14。ページ 2 の 0..83 行・0..111 ドット)
"""
import math, os, random, sys

W, H = 256, 188
NS = 32
img = [[1] * W for _ in range(H)]
rnd = random.Random(5005)
DECK, HULL_B, CLOUD = 40, 100, 112

def put(x, y, c):
    if 0 <= x < W and 0 <= y < H: img[y][x] = c

LIM = [None] * NS                       # (種類, 上, 下): 0 = 何もない、1 = 艦体(上..下)、2 = 室内(天井..床)

def base(k):
    x0 = k * 8
    for y in range(4):
        for x in range(8): put(x0 + x, y, 6)
    return x0

def clouds(x0, phase):
    """雲海(112..187)。周期 64 ドットの頂"""
    for x in range(8):
        gx = (x0 + x + phase) % 64
        top = CLOUD + int(6 + 4 * math.sin(gx / 64 * 2 * math.pi) + 3 * math.sin(gx / 16 * 2 * math.pi))
        for y in range(top, H):
            d = y - top
            c = 7 if d == 0 else 5 if d < 3 else 4 if d < 10 else 3
            if c == 4 and (gx * 5 + y * 3) % 13 == 0: c = 5
            put(x0 + x, y, c)

def hull(x0, top=DECK, portholes=False, deck_c=15):
    for x in range(8):
        for y in range(top, HULL_B):
            c = 14
            if y == top: c = deck_c
            elif y == top + 1: c = 9
            elif y >= HULL_B - 4: c = 2
            elif (y - DECK) % 14 == 0: c = 9                    # 板の継ぎ目
            if portholes and y in (58, 59) and x in (3, 4): c = 12
            put(x0 + x, y, c)

for k in range(8):                       # 0..7 空と雲海
    x0 = base(k); clouds(x0, 0); LIM[k] = (0, 0, 0)
for i, k in enumerate((8, 9, 10)):       # 艦首(だんだん高く)
    x0 = base(k); clouds(x0, 0)
    for x in range(8):
        t = (i * 8 + x) / 24
        top = int(DECK + 34 * (1 - t) ** 1.5)
        bot = int(HULL_B - 26 * (1 - t))
        for y in range(top, bot):
            put(x0 + x, y, 15 if y == top else 13 if y > bot - 5 else 14)
    LIM[k] = (1, DECK + 12 * (2 - i), HULL_B - 8 * (2 - i))
for k, ph in ((11, False), (12, True)):
    x0 = base(k); clouds(x0, 0); hull(x0, portholes=ph); LIM[k] = (1, DECK, HULL_B)
x0 = base(13); clouds(x0, 0); hull(x0)                   # 機銃の台(銃はスプライト)
for x in range(1, 7):
    for y in range(DECK - 4, DECK): put(x0 + x, y, 9 if y > DECK - 4 else 15)
LIM[13] = (1, DECK - 4, HULL_B)
for i, k in enumerate((14, 15, 16, 17)):                 # 主砲塔の台(32 ドット。上の 32x32 は GHOST で描く)
    x0 = base(k); clouds(x0, 0); hull(x0)
    for x in range(8):
        gx = i * 8 + x
        r = abs(gx - 15.5)
        for y in range(DECK - 8, DECK):
            if r < 15 - (DECK - y) * 0.6: put(x0 + x, y, 9 if (gx + y) % 5 else 14)
    LIM[k] = (1, DECK - 30, HULL_B)                       # 砲塔の上部も固い
for i, k in enumerate((18, 19)):                         # 格納庫の扉(閉)
    x0 = base(k); clouds(x0, 0); hull(x0, deck_c=12)
    for x in range(8):
        for y in range(DECK + 2, DECK + 30):
            put(x0 + x, y, 13 if ((x + y + i * 8) // 4) % 2 else 2)
    LIM[k] = (1, DECK, HULL_B)
x0 = base(20); clouds(x0, 0); hull(x0)                   # 格納庫の中(開)
for x in range(8):
    for y in range(DECK, DECK + 34): put(x0 + x, y, 2 if y < DECK + 32 else 9)
LIM[20] = (1, DECK + 34, HULL_B)
x0 = base(21); clouds(x0, 0)                             # 誘爆の穴(艦体が無い。縁に破片)
for y in (DECK, DECK + 1, HULL_B - 2, HULL_B - 1):
    for x in range(8):
        if rnd.random() < 0.4: put(x0 + x, y, 2)
LIM[21] = (0, 0, 0)
x0 = base(22); clouds(x0, 0); hull(x0)                   # 吊り砲台の台(艦底)
for x in range(1, 7):
    for y in range(HULL_B, HULL_B + 4): put(x0 + x, y, 9)
LIM[22] = (1, DECK, HULL_B + 4)
# 機関部: 天井 4..43(プレスの歯 / 溝)、通路 44..143、床 144..187
CEIL, FLOOR = 44, 144
def interior(x0, tooth_c, tooth_f, pipes=True, shaft=False):
    for x in range(8):
        for y in range(4, H):
            if y < CEIL:
                body = (tooth_c and True) or y < CEIL - 10
                c = (6 if x in (0, 7) else 14 if x in (2, 5) else 9) if body else 1
                if body and tooth_c and y >= CEIL - 4: c = 13 if y < CEIL - 1 else 2
                if not tooth_c and y == CEIL - 11: c = 2
                if shaft: c = 1
            elif y >= FLOOR:
                body = (tooth_f and True) or y >= FLOOR + 10
                c = (6 if x in (0, 7) else 14 if x in (2, 5) else 9) if body else 1
                if body and tooth_f and y < FLOOR + 4: c = 13 if y > FLOOR else 2
                if not tooth_f and y == FLOOR + 10: c = 2
            else:
                c = 1
                if pipes and y in (70, 71, 120, 121): c = 9
                if pipes and y == 96 and x in (2, 3): c = 12
            put(x0 + x, y, c)
x0 = base(23); interior(x0, True, True)                  # 装甲の破れ目(左の縁)
for y in range(4, H):
    if rnd.random() < 0.5: put(x0, y, 2)
LIM[23] = (2, CEIL, FLOOR)
x0 = base(24); interior(x0, True, True); LIM[24] = (2, CEIL, FLOOR)
x0 = base(25); interior(x0, True, True, pipes=False); LIM[25] = (2, CEIL, FLOOR)    # 歯
x0 = base(26); interior(x0, False, False, pipes=False); LIM[26] = (2, CEIL, FLOOR)  # 溝
x0 = base(27); interior(x0, False, True, shaft=True); LIM[27] = (2, 4, FLOOR)       # 出口の縦穴(上が開く)
for i, k in enumerate((28, 29, 30)):                     # 艦橋の土台
    x0 = base(k); clouds(x0, 0); hull(x0, deck_c=12)
    for x in range(8):
        for y in range(DECK - 6, DECK): put(x0 + x, y, 9 if y > DECK - 6 else 15)
    LIM[k] = (1, DECK - 6, HULL_B)
x0 = base(31); clouds(x0, 0)                             # 艦尾(低くなる)
for x in range(8):
    top = DECK + x * 3
    for y in range(top, HULL_B - x * 2): put(x0 + x, y, 15 if y == top else 14)
LIM[31] = (1, DECK + 12, HULL_B - 4)
assert all(LIM)

# ---- 地図 ----
cmap, SEC = [], {}
AA, TUR, HANG = [], [], []
def sec(name): SEC[name] = len(cmap)
sec('APPROACH'); cmap += [i & 7 for i in range(96)]
sec('BOW'); cmap += [8, 9, 10]
for i in range(93):
    if i % 12 == 6: AA.append(len(cmap)); cmap.append(13)
    else: cmap.append(11 + (i % 3 == 0))
sec('DECK')
for i in range(160):
    j = i % 40
    if j == 16: TUR.append(len(cmap))
    if 16 <= j < 20: cmap.append(14 + j - 16)
    elif i % 20 == 8: AA.append(len(cmap)); cmap.append(13)
    else: cmap.append(11 + (i % 4 == 1))
sec('HANGAR'); cmap += [11, 12] * 8
DOOR = len(cmap); cmap += [18, 19] * 4
cmap += [12, 11] * 8
sec('UNDER')
for i in range(96):
    if i % 12 == 6: HANG.append(len(cmap)); cmap.append(22)
    else: cmap.append(11 + (i % 5 == 2))
sec('ENGINE'); cmap += [23]
for i in range(87):
    j = i % 12
    cmap.append(25 if j < 4 else 26 if j < 8 else 24)
cmap += [27] * 8
sec('BOSS'); cmap += [11] * 8 + [28, 29, 30, 29, 30, 29, 30, 28] + [11] * 24
sec('STERN'); cmap += [12] * 12 + [31] * 4
SEC['END'] = len(cmap)
assert len(cmap) == 640, len(cmap)

# ---- 主砲塔の上部(32x32、16 向き: 0 = 左 → 15 = 右、真上を通る) ----
def turret_frames():
    N = 32
    sheet = [[0] * 256 for _ in range(64)]
    for d in range(16):
        a = math.pi - d * math.pi / 15                         # 180° → 0°
        ox, oy = (d % 8) * 32, (d // 8) * 32
        for y in range(N):
            for x in range(N):
                dx, dy = x - 15.5, y - 23.5
                c = 1                                          # 空(色 1)
                if dx * dx + dy * dy < 100 and dy < 6: c = 14 if dx * dx + dy * dy < 64 else 9
                # 砲身 2 本(向き a)
                ux, uy = math.cos(a), -math.sin(a)
                t = dx * ux + dy * uy; n = -dx * uy + dy * ux
                if 6 < t < 15.5 and (abs(n - 2) < 1.1 or abs(n + 2) < 1.1): c = 15 if t > 14 else 9
                if y >= 30: c = 9 if abs(dx) < 12 else 1             # 台との継ぎ目
                sheet[oy + y][ox + x] = c
    return sheet

# ---- 遠くの戦艦(8 段) ----
FAR = [(16, 4), (24, 5), (32, 7), (48, 10), (64, 13), (96, 19), (128, 26), (176, 35)]
FAR_AT = [(232, 89), (208, 89), (176, 89), (176, 78), (176, 64), (128, 100), (0, 100), (0, 64)]
def far_frames():
    sheet = [[0] * 256 for _ in range(128)]
    for (w, h), (ox, oy) in zip(FAR, FAR_AT):
        for y in range(h):
            for x in range(w):
                u, v = x / w, y / h                            # 0..1
                c = 1
                hull_top = 0.45 + 0.2 * max(0, 0.15 - u) / 0.15   # 艦首が低い
                if v > hull_top and v < 0.92 and u > 0.03 * (v * 4) and u < 0.98: c = 2 if w < 64 else 14
                if 0.25 < v <= hull_top and 0.55 < u < 0.7: c = 2 if w < 64 else 9            # 艦橋
                if 0.32 < v <= hull_top and (0.2 < u < 0.27 or 0.35 < u < 0.42 or 0.78 < u < 0.85): c = 2 if w < 64 else 9  # 砲塔
                if w >= 96 and abs(v - (hull_top + 0.02)) < 1.0 / h and c == 14: c = 15
                sheet[oy + y][ox + x] = c
    return sheet[64:128]

# ---- スプライト(機銃 8 向き・吊り砲台 3 向き) ----
def spr_frames():
    out = bytearray()
    def pat(fn):
        rows = []
        for y in range(16):
            v = 0
            for x in range(16):
                v = (v << 1) | (1 if fn(x - 7.5, y - 7.5) else 0)
            rows.append(v)
        b = bytearray()
        for half in (0, 1):                                    # 16x16 の並び: 左上・左下・右上・右下
            pass
        for y in range(16): b.append((rows[y] >> 8) & 255)
        for y in range(16): b.append(rows[y] & 255)
        return bytes(b)
    for d in range(8):                                         # 機銃: 0 = 左 → 7 = 右(上の半円)
        a = math.pi - d * math.pi / 7
        ux, uy = math.cos(a), -math.sin(a)
        out += pat(lambda dx, dy: (dx * dx + (dy - 4) ** 2 < 14 and dy > 1) or
                   (0 < dx * ux + (dy - 3) * uy < 9 and abs(-dx * uy + (dy - 3) * ux) < 1.2))
    for d in range(3):                                         # 吊り砲台: 0 = 左下、1 = 真下、2 = 右下
        a = -math.pi * 0.75 + d * math.pi * 0.25
        ux, uy = math.cos(a), -math.sin(a)
        out += pat(lambda dx, dy: (dx * dx + (dy + 4) ** 2 < 14 and dy < -1) or
                   (0 < dx * ux + (dy + 3) * uy < 9 and abs(-dx * uy + (dy + 3) * ux) < 1.2))
    return bytes(out)

# ---- GAUGE BLOCK のブロック(長さ 32..112、高さ 14) ----
GLEN = [112, 64, 96, 32, 80, 48]
def gauge_sheet():
    s = [[0] * 112 for _ in range(84)]
    for i, L in enumerate(GLEN):
        for y in range(14):
            for x in range(112):
                c = 1
                if x < L:
                    c = 15 if y == 0 else 2 if y == 13 else 14
                    if x in (0, L - 1): c = 9
                    if y in (6, 7) and x % 16 == 8: c = 12           # 刻印
                s[i * 14 + y][x] = c
    return s

def pack(rows):
    out = bytearray()
    for r in rows:
        for x in range(0, len(r), 2): out.append((r[x] << 4) | r[x + 1])
    return bytes(out)

def main(d, hpath):
    open(os.path.join(d, 's5_strips.bin'), 'wb').write(pack(img))
    open(os.path.join(d, 's5_map.bin'), 'wb').write(bytes(cmap))
    open(os.path.join(d, 'turret.bin'), 'wb').write(pack(turret_frames()))
    open(os.path.join(d, 'far.bin'), 'wb').write(pack(far_frames()))
    open(os.path.join(d, 's5_spr.bin'), 'wb').write(spr_frames())
    open(os.path.join(d, 'gauge.bin'), 'wb').write(pack(gauge_sheet()))
    with open(hpath, 'w') as f:
        f.write('/* s5_data.h — tools/gen_stage5.py が生成(手で編集しない)。5 面の区間と当たり。 */\n')
        f.write('#ifndef S5_DATA_H\n#define S5_DATA_H\n\n')
        for k, v in SEC.items(): f.write(f'#define S5_COL_{k} {v}\n')
        f.write(f'#define S5_COLS {len(cmap)}\n#define S5_COL_DOOR {DOOR}\n')
        f.write(f'#define S5_DECK {DECK}\n#define S5_HULL_B {HULL_B}\n#define S5_CEIL {CEIL}\n#define S5_FLOOR {FLOOR}\n')
        f.write(f'#define S5_NAA {len(AA)}\n#define S5_NTUR {len(TUR)}\n#define S5_NHANG {len(HANG)}\n\n')
        f.write('static const u8 s5_kind[32] = { ' + ', '.join(str(a) for a, _, _ in LIM) + ' };\n')
        f.write('static const u8 s5_top[32] = { ' + ', '.join(str(b) for _, b, _ in LIM) + ' };\n')
        f.write('static const u8 s5_bot[32] = { ' + ', '.join(str(c) for _, _, c in LIM) + ' };\n')
        f.write('static const u16 s5_aa[S5_NAA] = { ' + ', '.join(map(str, AA)) + ' };      /* 機銃の列 */\n')
        f.write('static const u16 s5_tur[S5_NTUR] = { ' + ', '.join(map(str, TUR)) + ' };   /* 主砲塔の左の列 */\n')
        f.write('static const u16 s5_hang[S5_NHANG] = { ' + ', '.join(map(str, HANG)) + ' };  /* 吊り砲台の列 */\n')
        f.write('static const u8 s5_far[8][4] = { ' + ', '.join(f'{{ {ox}, {oy - 64}, {w}, {h} }}' for (w, h), (ox, oy) in zip(FAR, FAR_AT)) + ' };   /* 遠くの戦艦: シートの x, y, 幅, 高さ */\n')
        f.write('static const u8 s5_glen[6] = { ' + ', '.join(map(str, GLEN)) + ' };   /* GAUGE BLOCK の長さ */\n')
        f.write('\n#endif /* S5_DATA_H */\n')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
