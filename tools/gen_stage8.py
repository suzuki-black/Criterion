#!/usr/bin/env python3
"""gen_stage8.py — 8 面「原器」(指示書 5.8)の絵と地図を作る。外部の画像は使わず、手続きで描く。

  usage: gen_stage8.py <出力ディレクトリ> <s8_data.h>

1. s8_strips.bin: 短冊 32 本(8x188、4bpp)。母艦の中枢の回廊(天井 4..27、床 160..187)。
     0..7 回廊(天井と床に目盛りの刻まれた板)/ 8..11 縦穴の底(天井が開いている)/ 12..15 原器の間(広い。
     天井 4..11、床 176..187)/ 16..19 崩れた回廊(脱出)/ 20..27 中ボスの間(回廊)/ 28..31 縦穴の底の左の壁
   ★0..3 行は色 6(ブロック 23)。板の縁は色 3(パレットアニメで脈打つ)。
2. s8_map.bin: 地図 640 列(縦穴の底 16 → 歪曲回廊 192 → 中ボスの間 40 → 中枢 96 → 原器の間 64 → 脱出 232)
3. s8_data.h: 区間の列、短冊ごとの天井と床
4. tbar.bin: TOLERANCE のバーの縁(256x6。目盛りつき)と縦穴の壁(64x40。ページ 2 の 6..45 行)
5. bell_i.bin: 内の鐘の ROTOR の元(64x64、4bpp。ガラスの継ぎ目つき)。bell96.bin: 外の鐘の 96x96 の 4 コマ(C3。ROTOR を使わず写す)
"""
import math, os, random, sys

W, H = 256, 188
img = [[1] * W for _ in range(H)]
rnd = random.Random(8008)
LIM = []

def put(x, y, c):
    if 0 <= x < W and 0 <= y < H: img[y][x] = c

def base(k):
    x0 = k * 8
    for y in range(4):
        for x in range(8): put(x0 + x, y, 6)
    return x0

def plate(x0, y0, y1, k, face):
    for x in range(8):
        for y in range(y0, y1):
            c = 14 if (y // 4 + k) % 2 else 9
            if y == (y1 - 1 if face > 0 else y0): c = 3      # ★縁は色 3(s8.c / s8b.c が鼓動のように光らせる)
            if (x + k * 8) % 16 == 0 and abs(y - (y1 - 1 if face > 0 else y0)) < 4: c = 2      # 目盛り
            put(x0 + x, y, c)

for k in range(8):
    x0 = base(k); plate(x0, 4, 28, k, 1); plate(x0, 160, H, k, -1); LIM.append((28, 160))
for k in range(8, 12):
    x0 = base(k); plate(x0, 160, H, k, -1); LIM.append((4, 160))
for k in range(12, 16):
    x0 = base(k); plate(x0, 4, 12, k, 1); plate(x0, 176, H, k, -1); LIM.append((12, 176))
for k in range(16, 20):
    x0 = base(k)
    t = 28 + rnd.randrange(-6, 10); b = 160 + rnd.randrange(-10, 6)
    plate(x0, 4, t, k, 1); plate(x0, b, H, k, -1)
    for i in range(3): put(x0 + rnd.randrange(8), rnd.randrange(t + 4, b - 4), 13)
    LIM.append((t, b))
for k in range(20, 28):
    x0 = base(k); plate(x0, 4, 28, k, 1); plate(x0, 160, H, k, -1); LIM.append((28, 160))

# ---- 縦穴の壁(2026-11-17。仮の灰色の板から作り直した) ----
# 母艦の中枢へ降りる縦穴の左右の壁(幅 32)。縦に 32 行の周期(行 = s & 31。高い空のブロックも縦穴の底の部屋も同じ位相)。
#   外から: 継ぎ目の暗い溝 → 装甲板(色 9、上の縁が 14 で光り、下の縁が 0 の影。鋲)→ 管(14、光る筋 15)→ 溝 → 目盛りの定規
#   (色 14 の地に 8 行ごとの刻み、32 行ごとの長い刻みは 15)→ 内側の縁(色 3。s8.c が中枢の鼓動のように光らせる)
#   右の壁は左右を裏返す。★高い空のブロック(stage.c の shaft_block)はページ 2 の 6..45 行(tbar.bin の下)から写す
WALL_P = 32
def wall_px(x, r):
    """左の壁の (x, 行) の色(x = 0..31、r = 0..31)"""
    if x <= 1: return 0 if r % 16 else 9                   # 外の溝
    if x <= 18:
        if r == 0: return 14                                # 板の上の縁(光)
        if r == WALL_P - 1: return 0                        # 板の下の縁(影)
        if x == 2 or x == 18: return 6
        if r in (4, 27) and x in (5, 15): return 15         # 鋲
        if 9 <= r <= 22 and 7 <= x <= 13: return 6 if (r + x) % 2 else 9   # 板の凹み(網目)
        return 9
    if x <= 21: return (15 if x == 20 else 14) if r % 8 else 6   # 管(8 行ごとに継ぎ手)
    if x == 22: return 0
    if x <= 28:                                             # 目盛りの定規
        if r % 32 == 0 and x >= 23: return 15
        if r % 8 == 0 and x >= 26: return 2
        if r % 4 == 0 and x >= 28: return 2
        return 14 if x < 28 else 9
    return 3                                                # 内側の縁(29..31)
def wall_rows(n, right):
    return [[wall_px(31 - x if right else x, r % WALL_P) for x in range(32)] for r in range(n)]

# 縦穴の底の部屋の左の 4 列(短冊 28..31): 床まで左の壁が続く(右は回廊へ開いている)
for k in range(28, 32):
    x0 = base(k)
    for y in range(4, 160):
        for x in range(8): put(x0 + x, y, wall_px((k - 28) * 8 + x, y % WALL_P))
    plate(x0, 160, H, k, -1); LIM.append((4, 160))

cmap, SEC = [], {}
def sec(n): SEC[n] = len(cmap)
sec('SHAFT'); cmap += [28 + i if i < 4 else 8 + (i & 3) for i in range(32)]   # 左の 4 列は壁
sec('WARP'); cmap += [i & 7 for i in range(176)]
sec('MID'); cmap += [20 + (i & 7) for i in range(40)]
sec('CORE'); cmap += [i & 7 for i in range(96)]
sec('BOSS'); cmap += [12 + (i & 3) for i in range(96)]
sec('ESCAPE'); cmap += [16 + (i & 3) for i in range(640 - len(cmap))]
assert len(cmap) == 640

def pack(rows):
    out = bytearray()
    for r in rows:
        for x in range(0, len(r), 2): out.append((r[x] << 4) | r[x + 1])
    return bytes(out)

def tbar():
    """ページ 2 の 0..45 行: 0..5 = TOLERANCE のバーの縁、6..45 = 縦穴の壁(x 0..31 = 左、32..63 = 右。40 行 = 周期 32 + 8)"""
    s = [[14] * 256 for _ in range(6)] + [[0] * 256 for _ in range(40)]
    for r, (a, b) in enumerate(zip(wall_rows(40, False), wall_rows(40, True))): s[6 + r][0:32] = a; s[6 + r][32:64] = b
    for x in range(256):
        s[5][x] = 15
        if x % 16 == 0:
            for y in range(1, 5): s[y][x] = 2
        if x % 16 == 8:
            for y in range(3, 5): s[y][x] = 2
    return pack(s)

def bell(inner):
    s = [[1] * 64 for _ in range(64)]
    for y in range(64):
        for x in range(64):
            dx, dy = x - 31.5, y - 31.5
            r = math.hypot(dx, dy); a = math.atan2(dy, dx)
            r0, r1 = (10, 28) if not inner else (4, 26)
            if r0 < r < r1:
                c = 9 if (r1 - r) > 3 else 15                   # ガラスの縁は明るい
                if not inner and r < r0 + 3: c = 15
                seam = (a * 4 / math.pi) % 1.0                  # 継ぎ目(外 8 本 / 内 4 本)
                if inner: seam = (a * 2 / math.pi) % 1.0
                if seam < 0.08: c = 2
                if (dx + dy) > 20 and c == 9: c = 7             # ハイライト
                s[y][x] = c
    return pack(s)

BELL96_ROT = (0, 2, 4, 6)           # 外の鐘の 96x96 の 4 コマ(64 で一周。継ぎ目 8 本なので 0..33.75° で足りる)

def bell96():
    """C3: 外の鐘の 96x96 の 4 コマを直に描く(ROTOR で 1.5 倍にすると縁が角ばる)。回り方は ROTOR と同じ
       (元の角度 a の点が a + 回転 に来る)。★ハイライトは回さない(光は動かない)"""
    out = b''
    k = 1.5
    for rot in BELL96_ROT:
        th = rot * math.pi / 32
        s = [[1] * 96 for _ in range(96)]
        for y in range(96):
            for x in range(96):
                dx, dy = x - 47.5, y - 47.5
                r = math.hypot(dx, dy); a = math.atan2(dy, dx) - th
                r0, r1 = 10 * k, 28 * k
                if r0 < r < r1:
                    c = 9 if (r1 - r) > 3 * k else 15
                    if r < r0 + 3 * k: c = 15
                    if (a * 4 / math.pi) % 1.0 < 0.08: c = 2
                    if (dx + dy) > 20 * k and c == 9: c = 7
                    s[y][x] = c
        out += pack(s)
    return out

def main(d, hpath):
    open(os.path.join(d, 's8_strips.bin'), 'wb').write(pack(img))
    open(os.path.join(d, 's8_map.bin'), 'wb').write(bytes(cmap))
    open(os.path.join(d, 'tbar.bin'), 'wb').write(tbar())
    open(os.path.join(d, 'bell_i.bin'), 'wb').write(bell(True))
    open(os.path.join(d, 'bell96.bin'), 'wb').write(bell96())
    with open(hpath, 'w') as f:
        f.write('/* s8_data.h — tools/gen_stage8.py が生成(手で編集しない)。8 面の区間と当たり。 */\n')
        f.write('#ifndef S8_DATA_H\n#define S8_DATA_H\n\n')
        for k, v in SEC.items(): f.write(f'#define S8_COL_{k} {v}\n')
        f.write('#define S8_COLS 640\n\n')
        f.write('static const u8 s8_ceil[32] = { ' + ', '.join(str(a) for a, _ in LIM) + ' };\n')
        f.write('static const u8 s8_floor[32] = { ' + ', '.join(str(b) for _, b in LIM) + ' };\n')
        f.write('\n#endif /* S8_DATA_H */\n')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
