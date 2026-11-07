#!/usr/bin/env python3
"""gen_stage6.py — 6 面「日時計要塞」(指示書 5.6)の絵と地図を作る。外部の画像は使わず、手続きで描く。

  usage: gen_stage6.py <出力ディレクトリ> <s6_data.h>

画面: 上 48 ラインは SCREEN 12(YJK の惑星)、48..187 ラインがプレイエリア(SCREEN 5)。
  プレイエリアは世界の s = -72..67(カメラ s = -120)。★リングの 0..47 行(s ≡ 68..115)は YJK の偶数バイトの置き場
  なので、短冊は 0..67 行だけを使う(68 行から下はページ 3 の作業域: 歯車と砂時計の回転のコマ)。
  ★0..3 行は色 6(ブロック 23)。6 面も色 6 を空の色にする。
1. s6_strips.bin: 短冊 32 本(8x188、4bpp。使うのは 0..67 行)
     0..3 空 / 4, 5 外壁(凸・凹)/ 6..9 歯車の窪み(32 ドット)/ 10 砂粒虫の棚 / 11 影の区間の外壁 /
     12..15 砂時計の間の床 / 16..19 文字盤 / 20..22 数字の台座(24 ドット)/ 23..26 指針の間の床 / 27..31 予備(空)
2. s6_map.bin: 地図 640 列(空 64 → 外壁 160 → 影 128 → 砂時計 32 → 文字盤 128 → 指針 56 → 残り)
3. s6_data.h: 区間の列、短冊ごとの上端(自機の当たり)、歯車・砂粒虫・数字の列
4. yjk_even.bin / yjk_odd.bin: 惑星の帯(256x48 の YJK)。偶数バイト → ページ 0、奇数バイト → ページ 2 の 0..47 行
5. gear.bin: ROTOR の元(64x64、4bpp)。hg96.bin: 砂時計の 96x96 の 120 コマ(0° は丸ごと、ほかは 2 つ前のコマとの差分)
6. s6_spr.bin: スプライト 2 枚(砂粒虫・影の中の目)
"""
import math, os, random, sys

W, H = 256, 188
NS = 32
img = [[0] * W for _ in range(H)]
rnd = random.Random(6006)
WALL = 36

def put(x, y, c):
    if 0 <= x < W and 0 <= y < H: img[y][x] = c

TOP = [68] * NS                          # 自機が下りられる限り(上端の行)

def base(k, stars=True):
    x0 = k * 8
    for y in range(68):
        for x in range(8): put(x0 + x, y, 6 if y < 4 else 1)
    if stars:
        r = random.Random(k)
        for i in range(2):
            put(x0 + r.randrange(8), 4 + r.randrange(28), 15 if r.random() < 0.5 else 14)
    return x0

def wall(x0, top, window=False):
    for x in range(8):
        for y in range(top, 68):
            c = 4
            if y == top: c = 7
            elif y == top + 1: c = 5
            elif (y - top) % 8 == 0: c = 3
            elif ((y - top) // 8 + (x0 // 8)) % 2 == 0 and x == 0: c = 3
            if window and 44 <= y < 56 and x in (3, 4): c = 2
            put(x0 + x, y, c)

for k in range(4): base(k)
x0 = base(4); wall(x0, WALL - 6); TOP[4] = WALL - 6
x0 = base(5); wall(x0, WALL); TOP[5] = WALL
for i, k in enumerate((6, 7, 8, 9)):                    # 歯車の窪み(歯車は ROTOR のコマを写す)
    x0 = base(k); wall(x0, WALL)
    for x in range(8):
        gx = i * 8 + x
        for y in range(WALL - 2, 66):
            if (gx - 15.5) ** 2 + (y - 49.5) ** 2 < 15.5 ** 2: put(x0 + x, y, 2)
    TOP[k] = WALL - 2
x0 = base(10); wall(x0, WALL)                            # 砂粒虫の棚
for x in range(8): put(x0 + x, WALL - 2, 9); put(x0 + x, WALL - 1, 9)
TOP[10] = WALL - 2
x0 = base(11); wall(x0, WALL, window=True); TOP[11] = WALL
for k in range(12, 16):                                  # 砂時計の間の床
    x0 = base(k); wall(x0, 56); TOP[k] = 56
for i, k in enumerate(range(16, 20)):                    # 文字盤(縁に目盛り)
    x0 = base(k)
    for x in range(8):
        for y in range(52, 68):
            c = 14 if y > 53 else 15
            if y in (55, 56) and (x + i * 8) % 8 == 0: c = 2
            put(x0 + x, y, c)
    TOP[k] = 52
for i, k in enumerate((20, 21, 22)):                     # 数字の台座(数字はフォントで描く)
    x0 = base(k)
    for x in range(8):
        for y in range(44, 68):
            c = 14 if y >= 52 else 9 if 0 < x + i * 8 < 23 else 2
            if y == 44: c = 15
            put(x0 + x, y, c)
    TOP[k] = 44
for k in range(23, 27):                                  # 指針の間の床
    x0 = base(k, stars=False)
    for x in range(8):
        for y in range(60, 68): put(x0 + x, y, 2 if y > 60 else 9)
    TOP[k] = 60
for k in range(27, 32): base(k)

# ---- 地図 ----
cmap, SEC = [], {}
GEAR, MITE, NUM = [], [], []
def sec(n): SEC[n] = len(cmap)
sec('AIR'); cmap += [i & 3 for i in range(64)]
sec('WALL')
for i in range(160):
    j = i % 40
    if j == 20: GEAR.append(len(cmap))
    if 20 <= j < 24: cmap.append(6 + j - 20)
    elif j == 8 or j == 32: MITE.append(len(cmap)); cmap.append(10)
    else: cmap.append(4 if (i // 2) % 2 else 5)
sec('SHADOW')
for i in range(128):
    if i % 32 == 16: MITE.append(len(cmap)); cmap.append(10)
    else: cmap.append(11 if i % 6 == 3 else 4 if (i // 2) % 2 else 5)
sec('MID'); cmap += [12 + (i & 3) for i in range(32)]
sec('DIAL')
for i in range(128):
    j = i % 10
    if j == 4 and len(NUM) < 12: NUM.append(len(cmap))
    if NUM and len(cmap) - NUM[-1] < 3 and j in (4, 5, 6): cmap.append(20 + j - 4)
    else: cmap.append(16 + (i & 3))
sec('BOSS'); cmap += [23 + (i & 3) for i in range(56)]
sec('END'); cmap += [23 + (i & 3) for i in range(640 - len(cmap))]
assert len(cmap) == 640 and len(NUM) == 12

# ---- YJK の惑星(256x48、横は 256 でくり返す) ----
def planet_rgb(x, y):
    """0..31 の RGB。ガス惑星の縞と嵐、上が恒星に照らされて明るい"""
    v = y / 48
    turb = 3 * math.sin(2 * math.pi * x / 256 * 2 + y * 0.4) + 2 * math.sin(2 * math.pi * x / 256 * 5 + y * 0.9)
    band = math.sin((y + turb) * 0.45) * 0.5 + 0.5
    r, g, b = 22 + 8 * band, 15 + 7 * band, 8 + 5 * band
    dx, dy = (x - 180) / 22, (y - 30) / 7                # 大赤斑
    d = dx * dx + dy * dy
    if d < 1: r, g, b = r + 6 * (1 - d), g - 4 * (1 - d), b - 3 * (1 - d)
    light = 1.15 - 0.75 * v                               # 上ほど明るい
    return [max(0, min(31, c * light)) for c in (r, g, b)]

def yjk_planes():
    ev, od = bytearray(), bytearray()
    for y in range(48):
        row = bytearray()
        for gx in range(0, 256, 4):
            px = [planet_rgb(gx + i, y) for i in range(4)]
            ys = [min(31, max(0, round(p[2] / 2 + p[0] / 4 + p[1] / 8))) for p in px]
            R = sum(p[0] for p in px) / 4; G = sum(p[1] for p in px) / 4; B = sum(p[2] for p in px) / 4
            Y = B / 2 + R / 4 + G / 8
            J = max(-32, min(31, round(R - Y))); K = max(-32, min(31, round(G - Y)))
            J &= 63; K &= 63
            row += bytes([(ys[0] << 3) | (K & 7), (ys[1] << 3) | (K >> 3), (ys[2] << 3) | (J & 7), (ys[3] << 3) | (J >> 3)])
        ev += row[0::2]; od += row[1::2]
    return bytes(ev), bytes(od)

def pack(rows):
    out = bytearray()
    for r in rows:
        for x in range(0, len(r), 2): out.append((r[x] << 4) | r[x + 1])
    return bytes(out)

def gear():
    s = [[2] * 64 for _ in range(64)]
    for y in range(64):
        for x in range(64):
            dx, dy = x - 31.5, y - 31.5
            r = math.hypot(dx, dy); a = math.atan2(dy, dx)
            tooth = 26 + 4 * (1 if math.cos(a * 8) > 0.2 else 0)
            if r < tooth:
                c = 5 if r > 22 else 4
                if 9 < r < 17 and math.cos(a * 4) > 0.6: c = 2         # 抜き窓
                if r < 6: c = 7
                if r < 3: c = 3
                s[y][x] = c
    return pack(s)

def hg_dot(dx, dy, k=1):
    """砂時計の 1 ドットの色。dx, dy は中心からの位置(64x64 の大きさで k = 1)"""
    c = 1
    if abs(dy) > 25 * k and abs(dy) <= 29 * k and abs(dx) < 22 * k: c = 5 if abs(dy) < 28 * k else 3   # 枠
    elif abs(dy) <= 25 * k:
        w = (3 + 16 * (abs(dy) / (25 * k)) ** 1.4) * k        # 括れ
        if abs(abs(dx) - w) < 1.2 * (k ** 0.5): c = 15 if dx < 0 else 9   # ガラスの縁(太さは大きさほどには増やさない)
        elif abs(dx) < w and dy > 6 * k: c = 7                # 下に溜まった砂
        elif abs(dx) < w: c = 1
    if abs(dx) > 18 * k and abs(dx) < 21 * k and abs(dy) <= 25 * k: c = 3   # 支柱
    return c

HG_NF = 120                                 # 180° を 120 コマ = 1.5° ずつ(64 で一周)。s6boss.c の S6_HG_NF
# ★5 コマ(37° ずつ)・9 コマ(20° ずつ)・18 コマ(10° ずつ、1 コマを丸ごと書いて 12 フレーム)では回っているように見えなかった(遊んでの指摘)。
#   s6boss.c はリングの表と裏の半分へ交互に書くので、裏には 2 つ前のコマが残っている。そのコマとの違い(差分)だけを書く(1 コマ平均約 900 バイト・220 記録)。
#   36 コマ(5° ずつ)でも縁が 1 回に 4 ドット飛んで回って見えなかったので、∞ の紋章と同じく 1 回に 1 ドット強にした

def hourglass96():
    """C3: 96x96 の HG_NF コマを直に描く(ROTOR で 1.5 倍にすると縁が角ばる)。回り方は ROTOR と同じ
       (写し先の (x, y) → 元の (cos・x + sin・y, -sin・x + cos・y))。返すのはコマごとの 4bpp(4608 バイト)の並び"""
    out = []
    for k in range(HG_NF):
        a = k * 32 / HG_NF
        c, s = math.cos(a * math.pi / 32), math.sin(a * math.pi / 32)
        rows = []
        for y in range(96):
            row = []
            for x in range(96):
                X, Y = x - 47.5, y - 47.5
                row.append(hg_dot(c * X + s * Y, -s * X + c * Y, 1.5))
            rows.append(row)
        out.append(pack(rows))
    return out

def hg_stream(P):
    """hg96.bin: 差分を 256 バイトの塊で並べる(頭は入りの 0° を 1 行丸ごとの記録で)。
       差分 = 行 u8・バイトの位置 u8・長さ u8・中身(間が 4 バイト未満の違いはつなぐ)。0xFE = 塊の終わり、0xFF = コマの終わり(塊の残りは捨てる)。
       並び: d1, d2 .. d(N-1), d0(コマ f は f-2 から。0 と 1 は回り終えた後の N-2, N-1 から)、最後に入りの 1 回目だけの 0 → 1。
       返すのは (中身, d1 の塊の番号, d2 の塊の番号, 入りの 1 回目の 0 → 1 の塊の番号)"""
    def delta(a, b):
        recs = []
        for y in range(96):
            ra, rb = a[y * 48:y * 48 + 48], b[y * 48:y * 48 + 48]
            x = 0
            while x < 48:
                if ra[x] == rb[x]: x += 1; continue
                x0 = last = x
                while x < 48 and (ra[x] != rb[x] or x - last < 4):
                    if ra[x] != rb[x]: last = x
                    x += 1
                recs.append(bytes([y, x0, last - x0 + 1]) + rb[x0:last + 1])
                x = last + 1
        return recs
    def blocks(recs):
        out, cur = bytearray(), bytearray()
        for r in recs:
            if len(cur) + len(r) > 255:
                cur.append(0xFE); out += cur + bytes(256 - len(cur)); cur = bytearray()
            cur += r
        cur.append(0xFF); out += cur + bytes(256 - len(cur))
        return out
    n = len(P)
    body = blocks([bytes([y, 0, 48]) + P[0][y * 48:y * 48 + 48] for y in range(96)])
    d1 = len(body) // 256
    for f in list(range(1, n)) + [0]:
        body += blocks(delta(P[(f - 2) % n], P[f]))
        if f == 1: d2 = len(body) // 256
    first = len(body) // 256
    body += blocks(delta(P[0], P[1]))
    return bytes(body), d1, d2, first

def sprites():
    def pat(rows):
        bits = [int(r.replace('.', '0').replace('#', '1'), 2) for r in rows]
        return bytes([(b >> 8) & 255 for b in bits] + [b & 255 for b in bits])
    mite = ['................', '....########....', '.....######.....', '......####......', '.......##.......',
            '......####......', '.....######.....', '....########....', '...##########...', '..#..#....#..#..',
            '.#...#....#...#.', '................', '................', '................', '................', '................']
    eyes = ['................'] * 5 + ['...###....###...', '...###....###...'] + ['................'] * 9
    return pat(mite) + pat(eyes)

def main(d, hpath):
    open(os.path.join(d, 's6_strips.bin'), 'wb').write(pack(img))
    open(os.path.join(d, 's6_map.bin'), 'wb').write(bytes(cmap))
    ev, od = yjk_planes()
    open(os.path.join(d, 'yjk_even.bin'), 'wb').write(ev)
    open(os.path.join(d, 'yjk_odd.bin'), 'wb').write(od)
    open(os.path.join(d, 'gear.bin'), 'wb').write(gear())
    hg, hg_d1, hg_d2, hg_first = hg_stream(hourglass96())
    open(os.path.join(d, 'hg96.bin'), 'wb').write(hg)
    open(os.path.join(d, 's6_spr.bin'), 'wb').write(sprites())
    with open(hpath, 'w') as f:
        f.write('/* s6_data.h — tools/gen_stage6.py が生成(手で編集しない)。6 面の区間と当たり。 */\n')
        f.write('#ifndef S6_DATA_H\n#define S6_DATA_H\n\n')
        for k, v in SEC.items(): f.write(f'#define S6_COL_{k} {v}\n')
        f.write(f'#define S6_COLS {len(cmap)}\n#define S6_NGEAR {len(GEAR)}\n#define S6_NMITE {len(MITE)}\n#define S6_NNUM {len(NUM)}\n\n')
        f.write(f'#define S6_HG_NF {HG_NF}            /* 砂時計のコマ(180°) */\n#define S6_HG_FIRST {hg_first}       /* hg96.bin の入りの 1 回目(0 → 1)の塊 */\n#define S6_HG_D1 {hg_d1}          /* コマ 1 の差分の塊(頭の 0..D1-1 は入りの 0°) */\n#define S6_HG_D2 {hg_d2}          /* コマ 2 の差分の塊(1 の後に続く塊は、1 回目だけ入りの 0 → 1 なので) */\n\n')
        f.write('static const u8 s6_top[32] = { ' + ', '.join(map(str, TOP)) + ' };   /* 自機が下りられる限りの行(s) */\n')
        f.write('static const u16 s6_gear[S6_NGEAR] = { ' + ', '.join(map(str, GEAR)) + ' };   /* 歯車の窪みの左の列 */\n')
        f.write('static const u16 s6_mite[S6_NMITE] = { ' + ', '.join(map(str, MITE)) + ' };   /* 砂粒虫の棚の列 */\n')
        f.write('static const u16 s6_num[S6_NNUM] = { ' + ', '.join(map(str, NUM)) + ' };   /* 数字の台座の左の列 */\n')
        f.write('\n#endif /* S6_DATA_H */\n')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
