#!/usr/bin/env python3
"""gen_stage5.py — 5 面「規格戦艦ノルム」(指示書 5.5)の絵と地図を作る。外部の画像は使わず、手続きで描く。

  usage: gen_stage5.py <出力ディレクトリ> <s5_data.h>

縦の割り付け(短冊の行 = 世界の s): 甲板 40、艦体 40..95、艦底 96..99、雲海 112..187。
  甲板の上を飛ぶ区間はカメラ s = -60(上 60 行は高い空)、艦底の区間はカメラ s = 0(上半分が艦底、下半分が雲海)。
  ★0..3 行は色 6(stage.c のブロック 23)。5 面は色 6 を空の色にして見えなくする(艦の暗い色は 2)。
1. s5_strips.bin: 短冊 32 本(8x188、4bpp)
     0..3 空と雲海(周期 32 ドット。接近の間は世界を止めて帯で流すので、64 の約数でくり返す)/ 4, 5 甲板室と通風口 / 6, 7 副砲の砲郭
     8..10 艦首 / 11, 12 甲板 / 13 機銃の台 / 14..17 主砲塔の台(32 ドット)/ 18, 19 格納庫の扉 /
     20 格納庫の中 / 21 誘爆の穴 / 22 艦底の吊り砲台の台 / 23 装甲の破れ目 / 24 機関室 / 25, 26 プレスの歯と溝 /
     27 出口の縦穴 / 28..30 艦橋の土台 / 31 艦尾
2. s5_map.bin: 地図 640 列(接近 96 → 艦首 96 → 甲板 160 → 格納庫 40 → 艦底 96 → 機関部 96 → 艦橋 40 → 艦尾 16)
3. s5_data.h: 区間の列、短冊ごとの当たり(種類・上・下)、機銃・主砲塔・吊り砲台の列
4. turret.bin: 主砲塔の上部 32x32 × 16 向き(左 → 真上 → 右の半円。ページ 2 の 0..63 行へ、8 枚ずつ 2 段)
5. far.bin: 接近で遠くの戦艦が本編の艦首そのものになるまでの差分(第 4 バンクの farz.c が VRAM へ直に書く)と farz_data.h
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

CLOUD_A = [3, -4, 2, -2, 4, -3, 3, -1, 4, -2, 3, -4, 2, -3, 4, -2]
def clouds(x0, phase):
    """雲海(112..187)。短冊の並びがどう変わっても継ぎ目が切れないよう、両端の高さをそろえて中だけ短冊ごとに変える"""
    a = CLOUD_A[x0 // 8 % 16]
    for x in range(8):
        top = CLOUD + int(6 + a * math.sin((x + 0.5) / 8 * math.pi))
        for y in range(top, H):
            d = y - top
            c = 7 if d == 0 else 5 if d < 3 else 4 if d < 10 else 3
            if c == 4 and ((x0 + x) * 5 + y * 3) % 13 == 0: c = 5
            put(x0 + x, y, c)

# 艦の側面(上から): 甲板の面 40..44 / 舷縁 45 / 影 46 / 上部の舷側 47..57(舷窓)/ 帯 58, 59 / 装甲帯 60..77(鋲)/ 喫水の線 78 /
# 下の舷側 79..95(丸みの陰を網点で)/ 艦底 96..99。★色 4, 5 は雲のきめで回すので艦に使わない(s5.c)
SCUT_Y = 51
def hull_px(gx, y, top=DECK, deck_c=14):
    if y == top: return deck_c
    if y < DECK + 5:                                       # 甲板の面: 手前へ走る板の継ぎ目
        return 9 if (gx + (y - DECK) * 3) % 8 == 0 else 3
    if y == DECK + 5: return 15
    if y == DECK + 6: return 2
    if y < 58:
        return 14 if y < 56 or (gx + y) & 1 else 3
    if y == 58: return 2
    if y == 59: return 3
    if y < 78:                                             # 装甲帯: 板の縦の継ぎ目と鋲
        if gx % 32 == 0: return 2
        if y in (61, 76) and gx % 4 == 2: return 15
        if y < 64: return 3 if (gx + y) & 1 else 14
        return 3
    if y == 78: return 2
    if y == 79: return 13                                  # 喫水の線
    if y < 84: return 9
    if y < 90: return 9 if (gx + y) & 1 else 2
    if y < 96: return 2 if (gx * 3 + y) & 3 else 9
    return 2

def hull(x0, top=DECK, portholes=False, deck_c=14, gx0=0):
    for x in range(8):
        gx = gx0 + x
        for y in range(top, HULL_B):
            c = hull_px(gx, y, top, deck_c)
            if portholes and SCUT_Y <= y <= SCUT_Y + 2 and x in (3, 4):
                c = 2 if y > SCUT_Y else 3
                if y == SCUT_Y + 1 and x == 3: c = 12      # 灯り
            put(x0 + x, y, c)

for k in range(4):                       # 0..3 空と雲海(周期 32 ドット)
    x0 = base(k); clouds(x0, 0); LIM[k] = (0, 0, 0)
for i, k in enumerate((4, 5)):           # 甲板室(後ろの暗い箱)と舷側の通風口
    x0 = base(k); clouds(x0, 0); hull(x0, gx0=8 + i * 8)
    for x in range(8):
        gx = i * 8 + x
        for y in range(DECK - 12, DECK):
            h = DECK - y
            if (gx < 2 or gx > 13) and h > 7: continue
            if gx in (0, 15): continue
            c = 9 if h in (12, 7) else 2
            if 4 < gx < 11 and h in (3, 4) and gx & 1: c = 12   # 窓
            put(x0 + x, y, c)
        if 2 <= gx < 14:
            for y in range(48, 56): put(x0 + x, y, 2 if y & 1 else 3)   # 通風口のよろい戸
    LIM[k] = (1, DECK, HULL_B)
for i, k in enumerate((6, 7)):           # 副砲の砲郭(装甲帯の窪みと砲身)
    x0 = base(k); clouds(x0, 0); hull(x0, gx0=8 + i * 8)
    for x in range(8):
        gx = i * 8 + x
        for y in range(60, 78):
            d = abs(gx - 8) * 0.6 + abs(y - 68)
            if d < 8: put(x0 + x, y, 2)
            elif d < 9: put(x0 + x, y, 15 if y < 68 else 9)
        if gx < 11:
            for y in (67, 68): put(x0 + x, y, 15 if y == 67 else 14)   # 砲身(左向き)
        if gx == 0:
            for y in (66, 67, 68, 69): put(x0 + x, y, 14)
    LIM[k] = (1, DECK, HULL_B)
for i, k in enumerate((8, 9, 10)):       # 艦首(反り上がった舳先。下は後ろへ引く)
    x0 = base(k); clouds(x0, 0)
    for x in range(8):
        gx = i * 8 + x
        t = gx / 24
        top = int(DECK + 26 * (1 - t) ** 1.6)
        bot = int(HULL_B - 34 * (1 - t) ** 1.2)
        for y in range(top, bot):
            c = hull_px(gx + 8, max(y, DECK + 1) if y < DECK + 5 else y)
            if y == top: c = 15
            elif y == top + 1: c = 2
            elif y >= bot - 1: c = 2
            elif y >= bot - 3: c = 9
            put(x0 + x, y, c)
        if gx in (14, 15) or (gx == 16):                   # 喫水の目盛り(規格の艦らしく物差し)
            for y in range(60, 94, 2):
                if y >= int(DECK + 26 * (1 - t) ** 1.6) + 2 and y < bot - 3:
                    if gx < 16 or y % 10 == 0: put(x0 + x, y, 15)
        if 18 <= gx < 22:                                  # 錨の穴
            for y in range(53, 58):
                put(x0 + x, y, 2)
    LIM[k] = (1, DECK + 12 * (2 - i), HULL_B - 8 * (2 - i))
for k, ph in ((11, False), (12, True)):
    x0 = base(k); clouds(x0, 0); hull(x0, portholes=ph, gx0=(k - 11) * 8); LIM[k] = (1, DECK, HULL_B)
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
for i, k in enumerate((28, 29, 30)):                     # 艦橋の土台(甲板から立ち上がる箱。窓が並ぶ)
    x0 = base(k); clouds(x0, 0); hull(x0, deck_c=14)
    for x in range(8):
        for y in range(DECK - 6, DECK + 1):
            c = 15 if y == DECK - 6 else 2 if y == DECK - 5 else 14 if y >= DECK - 1 else 12 if (x + i * 3) % 4 == 1 else 3
            if k == 28 and x < 2: c = 3
            put(x0 + x, y, c)
    LIM[k] = (1, DECK - 6, HULL_B)
x0 = base(31); clouds(x0, 0)                             # 艦尾(低くなる)
for x in range(8):
    top = DECK + x * 3
    for y in range(top, HULL_B - x * 2):
        put(x0 + x, y, 15 if y == top else hull_px(x + 8, max(y, DECK + 7)))
LIM[31] = (1, DECK + 12, HULL_B - 4)
assert all(LIM)

# ---- 地図 ----
cmap, SEC = [], {}
AA, TUR, HANG = [], [], []
def sec(name): SEC[name] = len(cmap)
sec('APPROACH'); cmap += [i & 3 for i in range(96)]
sec('BOW'); cmap += [8, 9, 10]
for i in range(93):
    if i % 12 == 6: AA.append(len(cmap)); cmap.append(13)
    elif i % 24 == 14: cmap.append(6)
    elif i % 24 == 15: cmap.append(7)
    elif i % 24 == 20: cmap.append(4)
    elif i % 24 == 21: cmap.append(5)
    else: cmap.append(11 + (i % 3 == 0))
sec('DECK')
for i in range(160):
    j = i % 40
    if j == 16: TUR.append(len(cmap))
    if 16 <= j < 20: cmap.append(14 + j - 16)
    elif i % 20 == 8: AA.append(len(cmap)); cmap.append(13)
    elif j in (6, 26): cmap.append(4)
    elif j in (7, 27): cmap.append(5)
    elif j in (32, 12): cmap.append(6)
    elif j in (33, 13): cmap.append(7)
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
sec('BOSS'); cmap += [11] * 4 + [6, 7] + [12, 11] + [4, 5] + [11] * 5 + [28] + [29, 30] * 7 + [28] + [11] * 9   # 塔(画面の x 184)の真下に土台
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

# ---- スプライト(機銃 8 向き・吊り砲台 3 向き) ----
def aa_fn(d):
    """機銃の向き d(0 = 左 → 7 = 右、上の半円)の形。(dx, dy) = 16x16 の中心からの位置"""
    a = math.pi - d * math.pi / 7
    ux, uy = math.cos(a), -math.sin(a)
    return lambda dx, dy: ((dx * dx + (dy - 4) ** 2 < 14 and dy > 1) or
                           (0 < dx * ux + (dy - 3) * uy < 9 and abs(-dx * uy + (dy - 3) * ux) < 1.2))

def spr_frames():
    out = bytearray()
    def pat(fn):
        rows = []
        for y in range(16):
            v = 0
            for x in range(16):
                v = (v << 1) | (1 if fn(x - 7.5, y - 7.5) else 0)
            rows.append(v)
        b = bytearray()                                        # 16x16 の並び: 左半分の 16 行・右半分の 16 行
        for y in range(16): b.append((rows[y] >> 8) & 255)
        for y in range(16): b.append(rows[y] & 255)
        return bytes(b)
    for d in range(8):                                         # 機銃: 0 = 左 → 7 = 右(上の半円)
        out += pat(aa_fn(d))
    for d in range(3):                                         # 吊り砲台: 0 = 左下、1 = 真下、2 = 右下
        a = -math.pi * 0.75 + d * math.pi * 0.25
        ux, uy = math.cos(a), -math.sin(a)
        out += pat(lambda dx, dy: (dx * dx + (dy + 4) ** 2 < 14 and dy < -1) or
                   (0 < dx * ux + (dy + 3) * uy < 9 and abs(-dx * uy + (dy + 3) * ux) < 1.2))
    return bytes(out)

# ---- GAUGE BLOCK のブロック(長さ 32..112、高さ 14) ----
GLEN = [112, 64, 96, 32, 80, 48]
def gauge_sheet():
    """ブロック = 艦橋の 1 層: 屋根 / 張り出した窓の帯 / 手すり / 壁(内へ引く)。一番短い 32 は弱点で窓が赤い"""
    s = [[0] * 112 for _ in range(84)]
    for i, L in enumerate(GLEN):
        win = 13 if L == 32 else 12
        for y in range(14):
            inset = 0 if y < 2 else 1 if y < 8 else 5 if y < 13 else 6
            for x in range(112):
                c = 1
                if inset <= x < L - inset:
                    if y == 0: c = 15
                    elif y == 1: c = 14
                    elif y == 2: c = 2
                    elif y < 7:
                        c = 2 if y in (3, 6) else win if x % 6 in (2, 3) else 2
                        if x in (inset, L - inset - 1): c = 3
                    elif y == 7: c = 15 if x & 1 else 3
                    elif y < 12:
                        c = 2 if y == 8 else 14 if (x + y) & 3 else 3
                        if x % 16 == 8: c = 3
                        if y == 10 and x % 8 == 4: c = 2
                    elif y == 12: c = 3
                    else: c = 2
                s[i * 14 + y][x] = c
    return s

# ---- 接近: 遠くの戦艦が近づいて、本編の艦首そのものになる(far.bin。第 4 バンクの farz.c が差分を VRAM へ直に書く) ----
# ★前は別に描いた「煙突とマストのある艦」を 57 段に拡大して閃光で消し、実寸の艦を別に流し込んでいた。形が本編と違い、
#   拡大の 1 段が縁で 3〜4 ドット動いてぎくしゃくし、同じ艦に見えなかった(遊んでの指摘)。
#   ・絵の元 = 本編の短冊・主砲塔の上部・機銃(スプライトの形と色)・GAUGE BLOCK の塔をそのまま並べた艦の側面。
#     実寸の艦は 4352 ドットあって遠くからは細い板にしか見えないので、遠景用に途中を抜いた艦(艦首 → 主砲塔 2 基 →
#     格納庫の扉 → 艦橋の塔 → 艦尾)にする。大きくなると見えるのは艦首から先の 300 ドットほどだけで、そこは本物と同じ。
#   ・縮めるのは面積の平均(積分画像)で色を混ぜてパレットのいちばん近い色へ(1 点を拾うと細部の色がコマごとに入れ替わって
#     ちらついた)。網点・鋲などの細かい模様は大きさに応じて 1 種類ずつ現れる(全部が最後のコマで一度に出ると目立つ)。
#   ・艦首の下の点(竜骨の行 100)を中心に、縁が 1 段に 1 ドットまでしか動かないように段を刻む(タイトルの ∞ と同じ考え)。
#     最後の段は s = 1(本編の短冊と 1 ドットも違わない)、艦首は画面の x = FZ_XB。そこで地図を差し替えて本物の列へすり替える(s5.c)。
#   ・艦首の左右の動きは絵に入れず、空の帯の横スクロール(表の d)で動かす(絵を動かすと全部の画素が替わってデータが 2 倍)。
#   ・差分は 1 つ前のコマとの比べ(片方だけの絵を書き換える。1 段の動きが 1 ドットなので書きかけが見えても目立たない)。
#     記録 = 行(0..105)・バイトの位置・長さ(最上位ビット = 同じバイトのくり返し、そのときの中身は 1 バイト)・中身。
#     0xFE = 次の 256 バイトの塊へ、0xFF = コマの終わり。記録は塊をまたがない。
FZ_XB, FZ_XF, FZ_PB = 160, 70, 0.45      # 最後の艦首の x / 最初の艦首の x / 艦首が動き終える段の進み
FZ_S0 = 0.1                              # 最初の大きさ
FZ_VT = -60                              # 絵の元の最上行(s)。塔のマストの先 -58 まで
FZ_H = 106                               # 書く行(画面 0..105。106 からは艦底と雲海の間の帯)
FZ_W = 256 - FZ_XF                       # 書く幅(艦首から。d の分だけ右まで見える)
AA_ROWS = [15, 12, 12, 12, 13, 13, 13, 13, 12, 12, 12, 13, 13, 15, 15, 15]   # 1 枚の敵の色(gen_sprites.py の WARM。5 面)
FZ_PAL = {2: (1, 1, 2), 3: (3, 3, 4), 9: (2, 3, 5), 12: (6, 5, 2), 13: (6, 3, 2), 14: (4, 4, 5), 15: (7, 7, 7)}

def fz_full():
    """艦首(BOW)から艦尾までの側面(行 FZ_VT..105、0 = 空)。本編で描くものと同じ"""
    c0 = SEC['BOW']; ncol = SEC['END'] - c0
    H = 106 - FZ_VT
    src = [[0] * (ncol * 8) for _ in range(H)]
    for i in range(ncol):
        k = cmap[c0 + i]
        for y in range(4, 106):
            for x in range(8):
                c = img[y][k * 8 + x]
                if c not in (1, 6): src[y - FZ_VT][i * 8 + x] = c
    tf = turret_frames()                                       # 主砲塔の上部(s5boss.c: 0..25 行を空で消してから向き 8 を写す)
    for col in TUR:
        x0 = (col - c0) * 8
        for y in range(32):
            for x in range(32):
                c = tf[32 + y][x]                              # 向き 8 = 2 段目の左端
                if y < 26: src[4 + y - FZ_VT][x0 + x] = 0 if c == 1 else c
                elif c != 1: src[4 + y - FZ_VT][x0 + x] = c
    fn = aa_fn(3)                                              # 機銃(スプライト。向き 3、左上 = 列の x - 4、DECK - 20 行)
    for col in AA:
        x0 = (col - c0) * 8 - 4
        for y in range(16):
            for x in range(16):
                if fn(x - 7.5, y - 7.5): src[DECK - 20 + y - FZ_VT][x0 + x] = AA_ROWS[y]
    gs = gauge_sheet()                                         # 艦橋の塔(GAUGE BLOCK。s5boss.c の g_row_s と G_AX = 184)
    ax = (SEC['BOSS'] - c0) * 8 + 184
    for k, top in enumerate((20, 6, -8, -22, -36, -50)):
        L = GLEN[k]
        for y in range(14):
            for x in range(L):
                c = gs[k * 14 + y][x]
                if c != 1: src[top + y - FZ_VT][ax - L // 2 + x] = c
    for y in range(-53, -50):
        for x in range(ax - 12, ax + 12): src[y - FZ_VT][x] = 2  # 測距儀
    for y in range(-58, -53):
        for x in range(ax - 2, ax + 2): src[y - FZ_VT][x] = 3    # マスト
    return src

def fz_src():
    """遠景用に途中を抜いた艦: 艦首 + 艦首の区間の 37 列 / 主砲塔 2 基 / 格納庫の扉 / 甲板 / 艦橋の区間と艦尾"""
    full = fz_full(); b = SEC['BOW']
    segs = [(0, 40)] + [(t - b - 6, t - b + 14) for t in TUR[:2]] + \
           [(DOOR - b - 4, DOOR - b + 12), (SEC['UNDER'] - b, SEC['UNDER'] - b + 8), (SEC['BOSS'] - b, SEC['END'] - b)]
    src = [[] for _ in full]
    for a, e in segs:
        for y, r in enumerate(full): src[y] += r[a * 8:e * 8]
    U = len(src[0])
    return src                                                 # ★艦尾の形は fz_model が基本の形で置き換える

# ---- 縮め方: 基本の形(艦体の帯)と部品に分けて、それぞれを丸めた位置へ置く ----
# ★画素ごとに面積の平均を取って色を決めると、細い線(装甲の継ぎ目・舷窓・白い甲板の線)や小さな物(機銃・甲板室)が
#   段ごとに出たり消えたりしてまたたいた(1 段の動きを 1 ドットにしても滑らかに見えない)。
#   ・艦体は行ごとに 1 色の帯(網点は画面の位置で決まる模様)。元の行 v は画面の行 R(v) = 100 + (v - 100) s を丸めた行へ写り、
#     同じ行へ写る行が重なったら細い線(白い甲板の線・喫水線など)が勝つ。→ 帯の境は 1 ドットずつ動くだけで、線は消えない。
#   ・艦首・艦尾は輪郭(上と下)を写して、縁の線は輪郭に沿って描く。
#   ・それ以外(s = 1 の基本の形と違う所)は 8 近傍でつながった部品にして、部品ごとに左上と大きさを丸めて置き、その中だけ面積の平均。
#     小さな部品(鋲・継ぎ目・舷窓)は大きさが十分になってから出す。
#   ・s = 1 では本編の短冊と 1 画素も違わない(far_stream が確かめる)。
FZ_BOW_W, FZ_STERN_W = 24, 32
FZ_THIN = {40: 5, 45: 6, 46: 4, 58: 3, 59: 2, 78: 3, 79: 7}   # 細い線の行と優先度(大きいほど勝つ)

def fz_band(v):
    """艦体の行 v の色(数字)か網点の種類(文字。hull_px と同じ模様を画面の位置で)"""
    if v == 40: return 14
    if v < 45: return 3
    if v == 45: return 15
    if v == 46: return 2
    if v < 56: return 14
    if v < 58: return 'a'
    if v == 58: return 2
    if v == 59: return 3
    if v < 64: return 'b'
    if v < 78: return 3
    if v == 78: return 2
    if v == 79: return 13
    if v < 84: return 9
    if v < 90: return 'c'
    if v < 96: return 'd'
    return 2

def fz_dith(b, X, Y):
    if b == 'a': return 14 if (X + Y) & 1 else 3
    if b == 'b': return 3 if (X + Y) & 1 else 14
    if b == 'c': return 9 if (X + Y) & 1 else 2
    if b == 'd': return 2 if (3 * X + Y) & 3 else 9
    return b

def fz_model():
    """(源の絵, 長さ U, 輪郭の関数, 部品の一覧)"""
    src = fz_src(); U = len(src[0]); SH = len(src)
    def shape(u):                                              # 列 u の (上, 下, 種類 0 = 艦首 / 1 = 艦体 / 2 = 艦尾)
        if u < FZ_BOW_W:
            t = u / 24
            return int(DECK + 26 * (1 - t) ** 1.6), int(HULL_B - 34 * (1 - t) ** 1.2), 0   # 短冊 8..10 と同じ式
        if u >= U - FZ_STERN_W:
            t = (u - (U - FZ_STERN_W)) / FZ_STERN_W
            return DECK + int(t * 24), HULL_B - int(t * 10), 2
        return DECK, HULL_B, 1
    def base_px(v, X, Y, top, bot, kind):
        if kind == 0:
            if v == top: return 15
            if v == top + 1: return 2
            if v >= bot - 1: return 2
            if v >= bot - 3: return 9
            return fz_dith(fz_band(max(v, 41) if v < 45 else v), X, Y)
        if kind == 2:
            return 15 if v == top else fz_dith(fz_band(max(v, 47)), X, Y)
        return fz_dith(fz_band(v), X, Y)
    base1 = [[0] * U for _ in range(SH)]
    for u in range(U):
        top, bot, kind = shape(u)
        for v in range(top, bot): base1[v - FZ_VT][u] = base_px(v, u, v, top, bot, kind)
    for u in range(U - FZ_STERN_W, U):                         # 艦尾は基本の形だけ(短冊 31 の 4 回くり返しはのこぎりに見える)
        for y in range(SH): src[y][u] = base1[y][u]
    mask = [[src[y][u] != base1[y][u] for u in range(U)] for y in range(SH)]
    seen = [[False] * U for _ in range(SH)]
    comps = []
    for y in range(SH):
        for u in range(U):
            if not mask[y][u] or seen[y][u]: continue
            st = [(y, u)]; seen[y][u] = True; px = []
            while st:
                cy, cu = st.pop(); px.append((cy, cu))
                for dy in (-1, 0, 1):
                    for du in (-1, 0, 1):
                        ny, nu = cy + dy, cu + du
                        if 0 <= ny < SH and 0 <= nu < U and mask[ny][nu] and not seen[ny][nu]:
                            seen[ny][nu] = True; st.append((ny, nu))
            ys = [q[0] for q in px]; us = [q[1] for q in px]
            c = dict(v0=min(ys) + FZ_VT, v1=max(ys) + 1 + FZ_VT, u0=min(us), u1=max(us) + 1,
                     px={(q[0] + FZ_VT, q[1]): src[q[0]][q[1]] for q in px})
            w, h, n = c['u1'] - c['u0'], c['v1'] - c['v0'], len(px)
            c['smin'] = 0.9 if n <= 2 else (0.45 if max(w, h) >= 8 else 0.8) if (w == 1 or h == 1) else 0.5 if n <= 8 else 0.0
            comps.append(c)
    return src, U, shape, comps

def fz_R(v, s): return int(math.floor(100 + (v - 100) * s + 0.5))

def fz_render(model, s, xlim):
    """大きさ s の絵(FZ_H 行 × FZ_W 列、艦首 = 列 0。0 = 空)。xlim 以上の列は描かない(None)"""
    src, U, shape, comps = model
    out = [[0] * FZ_W for _ in range(FZ_H)]
    rowv = {}                                                  # 画面の行 → そこへ写る艦体の行(細い線が勝つ)
    for v in range(DECK, HULL_B):
        y = fz_R(v, s)
        if y not in rowv or FZ_THIN.get(v, 0) > FZ_THIN.get(rowv[y], 0): rowv[y] = v
    for x in range(min(FZ_W, xlim)):
        uc = (x + 0.5) / s
        if uc >= U: break
        top, bot, kind = shape(int(uc))
        yt, yb = fz_R(top, s), fz_R(bot, s)
        for y in range(max(yt, 0), min(yb, FZ_H)):
            X = FZ_XB + x
            if kind == 1: c = fz_dith(fz_band(rowv.get(y, top)), X, y)
            elif y == yt: c = 15
            elif kind == 0 and y == yt + 1: c = 2
            elif kind == 0 and y >= yb - 1: c = 2
            elif kind == 0 and y >= yb - 3: c = 9
            else:
                v = rowv.get(y, 47)
                c = fz_dith(fz_band(max(v, 41) if kind == 0 else max(v, 47)), X, y)
            out[y][x] = c
    for c in comps:
        if s < c['smin'] - 1e-9: continue
        x0 = int(math.floor(s * c['u0'] + 0.5)); x1 = max(x0 + 1, int(math.floor(s * c['u1'] + 0.5)))
        if x0 >= min(FZ_W, xlim): continue
        y0 = fz_R(c['v0'], s); y1 = max(y0 + 1, fz_R(c['v1'], s))
        w = c['u1'] - c['u0']; wn = x1 - x0
        rows = {}
        for v in range(c['v0'], c['v1']):
            rows.setdefault(min(max(fz_R(v, s), y0), y1 - 1), []).append(v)
        for y in range(max(y0, 0), min(y1, FZ_H)):
            vs = rows.get(y) or [min(c['v1'] - 1, max(c['v0'], int(100 + (y + 0.5 - 100) / s)))]
            for k in range(wn):
                x = x0 + k
                if x >= min(FZ_W, xlim): break
                ua = c['u0'] + k * w / wn; ub = c['u0'] + (k + 1) * w / wn
                us = range(int(ua), max(int(ua) + 1, int(math.ceil(ub))))
                n = tot = 0; rr = gg = bb = 0
                for v in vs:
                    for u in us:
                        tot += 1
                        cc = c['px'].get((v, u))
                        if cc:
                            n += 1; pr, pg, pb = FZ_PAL[cc]; rr += pr; gg += pg; bb += pb
                if n and n * 2 >= tot:
                    out[y][x] = min(FZ_PAL, key=lambda q: (FZ_PAL[q][0] - rr / n) ** 2 * 3 + (FZ_PAL[q][1] - gg / n) ** 2 * 6 + (FZ_PAL[q][2] - bb / n) ** 2)
    return out

def fz_scale(p): return FZ_S0 * (1 / FZ_S0) ** p
def fz_d(p):
    """艦首の左右の動き(空の帯の横スクロール)。最後の艦首の x から何ドット左か"""
    if p >= FZ_PB: return 0
    t = p / FZ_PB; t = t * t * (3 - 2 * t)
    return int(round((FZ_XB - FZ_XF) * (1 - t)))

def far_stream():
    """(far.bin, 段ごとの d)"""
    model = fz_model(); src, U = model[0], model[1]; SH = len(src)
    def pos(p, u, v):                                          # 艦首の下の点から見た画面の位置(艦首の左右の動きは除く)
        s = fz_scale(p)
        return s * u, (v - 100) * s
    tops = {}                                                  # 列 u のいちばん上の行(艦の輪郭の上端)
    for u in list(range(0, U, 4)) + [U - 1]:
        tops[u] = next((y + FZ_VT for y in range(SH) if src[y][u]), 100)
    def move(p0, p1):                                          # 1 段で画面に見える部分(輪郭の上端と竜骨)が動く最大のドット
        m = 0; d1 = fz_d(p1)
        for u, top in tops.items():
            if FZ_XB + pos(p1, u, 0)[0] - d1 > 258 and FZ_XB + pos(p0, u, 0)[0] - fz_d(p0) > 258: continue
            for v in (top, 100):
                a, b2 = pos(p0, u, v), pos(p1, u, v)
                m = max(m, abs(a[0] - b2[0]), abs(a[1] - b2[1]))
        return m
    ps = [0.0]
    while ps[-1] < 1:
        if move(ps[-1], 1.0) <= 1.0: ps.append(1.0); break
        lo, hi = ps[-1], 1.0
        for _ in range(30):
            mid = (lo + hi) / 2
            if move(ps[-1], mid) <= 1.0: lo = mid
            else: hi = mid
        ps.append(lo)
    def frame(p, prev):
        xlim = 256 + fz_d(p) - FZ_XB                           # 画面の右端(艦首からの列)。その先は前のまま
        cur = fz_render(model, fz_scale(p), xlim)
        return [cur[y][:xlim] + prev[y][xlim:] for y in range(FZ_H)]
    def nib(c): return c if c else 1                           # 空 = 色 1
    out = bytearray()
    def put_rec(rec):
        room = 256 - len(out) % 256
        if len(rec) + 1 > room:                                # ★印の 1 バイトは必ず残す
            out.append(0xFE); out.extend(bytes(room - 1))
        out.extend(rec)
    prev = [[0] * FZ_W for _ in range(FZ_H)]
    ds = []
    for p in ps:
        cur = frame(p, prev)
        for y in range(FZ_H):
            ba = [(nib(prev[y][i]) << 4) | nib(prev[y][i + 1]) for i in range(0, FZ_W, 2)]
            bb = [(nib(cur[y][i]) << 4) | nib(cur[y][i + 1]) for i in range(0, FZ_W, 2)]
            i = 0
            while i < len(bb):
                if ba[i] == bb[i]: i += 1; continue
                j, gap = i, 0                                  # 変わったバイトの並び(4 バイト未満の切れ目はつなぐ)
                while j < len(bb) and gap < 4:
                    gap = gap + 1 if ba[j] == bb[j] else 0
                    j += 1
                j -= gap
                q = i; lit = i
                while q < j:
                    r = q
                    while r < j and bb[r] == bb[q]: r += 1
                    if r - q >= 5:                             # くり返し(4 バイトの記録)
                        while lit < q:
                            n = min(q - lit, 127); put_rec(bytes((y, lit, n)) + bytes(bb[lit:lit + n])); lit += n
                        while q < r:
                            n = min(r - q, 127); put_rec(bytes((y, q, 0x80 | n, bb[q]))); q += n
                        lit = r
                    q = r
                while lit < j:
                    n = min(j - lit, 127); put_rec(bytes((y, lit, n)) + bytes(bb[lit:lit + n])); lit += n
                i = j
        out.append(0xFF)                                       # put_rec が 1 バイト残しているので塊に収まる
        ds.append(fz_d(p)); prev = cur
    assert ds[-1] == 0 and fz_scale(ps[-1]) == 1.0
    full = fz_full()                                           # ★最後の段は本編の艦首と 1 画素も違わない(すり替えが見えない)
    for y in range(FZ_H):
        for x in range(256 - FZ_XB):
            assert prev[y][x] == (full[y - FZ_VT][x] if y >= FZ_VT else 0), (x, y)
    return bytes(out), ds

def pack(rows):
    out = bytearray()
    for r in rows:
        for x in range(0, len(r), 2): out.append((r[x] << 4) | r[x + 1])
    return bytes(out)

def main(d, hpath):
    open(os.path.join(d, 's5_strips.bin'), 'wb').write(pack(img))
    open(os.path.join(d, 's5_map.bin'), 'wb').write(bytes(cmap))
    open(os.path.join(d, 'turret.bin'), 'wb').write(pack(turret_frames()))
    fz, fz_ds = far_stream()
    open(os.path.join(d, 'far.bin'), 'wb').write(fz)
    with open(os.path.join(os.path.dirname(hpath), 'farz_data.h'), 'w') as f:   # 第 4 バンクの farz.c だけが取り込む
        f.write('/* farz_data.h — tools/gen_stage5.py が生成(手で編集しない)。接近の戦艦の段ごとの艦首の動き(空の帯の横スクロール)。 */\n')
        f.write(f'#define FZ_NF {len(fz_ds)}\n#define FZ_XB {FZ_XB}\n')
        f.write('static const u8 fz_dtab[FZ_NF] = { ' + ', '.join(map(str, fz_ds)) + ' };\n')
    print(f'far.bin: {len(fz)} バイト、{len(fz_ds)} 段')
    open(os.path.join(d, 's5_spr.bin'), 'wb').write(spr_frames())
    open(os.path.join(d, 'gauge.bin'), 'wb').write(pack(gauge_sheet()))
    with open(hpath, 'w') as f:
        f.write('/* s5_data.h — tools/gen_stage5.py が生成(手で編集しない)。5 面の区間と当たり。 */\n')
        f.write('#ifndef S5_DATA_H\n#define S5_DATA_H\n\n')
        for k, v in SEC.items(): f.write(f'#define S5_COL_{k} {v}\n')
        f.write(f'#define S5_COLS {len(cmap)}\n#define S5_COL_DOOR {DOOR}\n')
        f.write(f'#define S5_DECK {DECK}\n#define S5_HULL_B {HULL_B}\n#define S5_CEIL {CEIL}\n#define S5_FLOOR {FLOOR}\n')
        f.write(f'#define S5_FZ_XB {FZ_XB}   /* 接近の最後の艦首の画面の x(farz.c) */\n')
        f.write(f'#define S5_NAA {len(AA)}\n#define S5_NTUR {len(TUR)}\n#define S5_NHANG {len(HANG)}\n\n')
        f.write('static const u8 s5_kind[32] = { ' + ', '.join(str(a) for a, _, _ in LIM) + ' };\n')
        f.write('static const u8 s5_top[32] = { ' + ', '.join(str(b) for _, b, _ in LIM) + ' };\n')
        f.write('static const u8 s5_bot[32] = { ' + ', '.join(str(c) for _, _, c in LIM) + ' };\n')
        f.write('static const u16 s5_aa[S5_NAA] = { ' + ', '.join(map(str, AA)) + ' };      /* 機銃の列 */\n')
        f.write('static const u16 s5_tur[S5_NTUR] = { ' + ', '.join(map(str, TUR)) + ' };   /* 主砲塔の左の列 */\n')
        f.write('static const u16 s5_hang[S5_NHANG] = { ' + ', '.join(map(str, HANG)) + ' };  /* 吊り砲台の列 */\n')
        f.write('static const u8 s5_glen[6] = { ' + ', '.join(map(str, GLEN)) + ' };   /* GAUGE BLOCK の長さ */\n')
        f.write('\n#endif /* S5_DATA_H */\n')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
