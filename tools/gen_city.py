#!/usr/bin/env python3
"""gen_city.py — 1 面の都市(指示書 5.1)の絵と地図を作る。外部の画像は使わず、手続きで描く。

  usage: gen_city.py <出力ディレクトリ> <city_data.h>

1. 短冊(strip): 幅 8 x 高さ 188 の縦長の絵を 32 本(256x188、SCREEN5 の 4bpp)→ city_strips.bin。
   ページ 3 の 0..187 行に置き、都市を流すときは 1 列(8 ドット)ごとに 1 本を HMMM で表示リングへ写す。
   行 s(0..187)は都市の画面の行と同じ(都市では R#23 = 188 なので、リングの行 = (188 + s) & 255)。
     0..13   雲の底(降下で突き抜けた雲が天井に残る)。上 8 行は近い雲と同じ色 6
     14..157 空(色 1。ラスターで帯ごとに色を変える)とビル
     158     歩道(色 14)
     159..187 街路(色 0 = 黒地。Pawn が歩く。M3)
   使う色: 0 黒 / 1 空 / 6 ビル(近い雲と同じ) / 7 屋上の縁の光 / 8 橙の窓 / 9 青灰 / 12 黄の窓 / 13 赤 / 14 灰。
   ★2..5 は降下の間まだ画面にある遠い雲・中の雲の色なので使わない。
2. 地図: 都市の列(8 ドット)ごとの短冊の番号 → city_map.bin(1 列 1 バイト)。
   区間: 降下(32 列)/ 都市前半(192)/ 中ボスの広場(40)/ 都市後半・高架道路(160)/ ボスの広場(40)/ 上昇(64)。
3. city_data.h: 区間の列の位置と、砲台(Spirit)を置く屋上の (列, 行)。
"""
import os, random, sys

W, H = 256, 188
SKY, BODY, RIM, ORANGE, BLUEGRAY, YELLOW, RED, GRAY = 1, 6, 7, 13, 9, 12, 13, 14   # ★8 はスプライトの縁取り(暗い)に使うので、橙の窓は 13
WALK, STREET0 = 158, 159
CLOUD_SOLID, CLOUD_END = 8, 14

rnd = random.Random(1999)
img = [[SKY] * W for _ in range(H)]

def put(x, y, c):
    if 0 <= x < W and 0 <= y < H: img[y][x] = c

def base(k):
    """短冊 k の共通部分: 雲の底・空・歩道・街路"""
    x0 = k * 8
    bump = [2, 3, 5, 6, 6, 5, 3, 2]                       # 短冊の両端で高さがそろう(どの順に並べても段差が出ない)
    for x in range(8):
        for y in range(CLOUD_END):
            if y < CLOUD_SOLID + bump[x] - 2: put(x0 + x, y, BODY)
            elif y < CLOUD_SOLID + bump[x] and (x + y) & 1: put(x0 + x, y, BODY)
        put(x0 + x, WALK, GRAY)
        for y in range(STREET0, H): put(x0 + x, y, 0)
        if 2 <= x <= 5: put(x0 + x, 173, BLUEGRAY)          # 車線の破線(8 ドットおき)

def windows(x0, xs, top, seed_extra=0, holes=0):
    r = random.Random(k_seed * 131 + seed_extra)
    for wy in range(top + 4, WALK - 5, 7):
        for wx in xs:
            v = r.random()
            c = YELLOW if v < 0.35 else ORANGE if v < 0.5 else 0
            if holes and r.random() < holes: c = SKY        # 壊れて空が見える
            for dy in range(3):
                put(x0 + wx, wy + dy, c); put(x0 + wx + 1, wy + dy, c)

def building(k, top, kind, jag=0, holes=0.0):
    x0 = k * 8
    xr = {'L': range(2, 8), 'R': range(0, 6), 'W': range(0, 8)}[kind]
    tops = {}
    for x in xr:
        t = top + (rnd.randrange(jag) if jag else 0)
        tops[x] = t
        for y in range(t, WALK): put(x0 + x, y, BODY)
        put(x0 + x, t, RIM)                                  # 屋上の縁(夕日)
    if kind == 'L':
        for y in range(top + 1, WALK): put(x0 + 2, y, RIM if (y & 3) == 0 else BODY)
    xs = {'L': [4], 'R': [1], 'W': [1, 5]}[kind]
    windows(x0, xs, max(tops.values()), holes=holes)

STRIPS = []      # (説明, 屋上の行 or None)
k_seed = 0
def strip(desc, fn, roof=None):
    global k_seed
    k = len(STRIPS)
    k_seed = k
    base(k)
    if fn: fn(k)
    STRIPS.append((desc, roof))
    return k

EMPTY = strip('empty', None)
HEIGHTS = [40, 70, 100, 125]
BLD = {}
for t in HEIGHTS:
    BLD[t] = [strip(f'b{t}{kind}', (lambda kk, t=t, kind=kind: building(kk, t, kind[0])), t) for kind in ('L', 'W1', 'W2', 'R')]
RUIN = [strip(f'ruin{t}', (lambda kk, t=t: building(kk, t, 'W', jag=12, holes=0.25)), t + 6) for t in (70, 70, 100, 100)]

def antenna(kk):
    building(kk, 40, 'W')
    x0 = kk * 8
    put(x0 + 3, 41, RED); put(x0 + 4, 41, RED)               # ★屋上の赤い灯だけ(25..39 行へ伸びる柱は外した: 遠景の帯 16..39 に入る)
ANTENNA = strip('antenna', antenna, 40)

ROAD_Y = 118
def road(kk, pillar):
    x0 = kk * 8
    for x in range(8):
        put(x0 + x, ROAD_Y, GRAY)
        for y in range(ROAD_Y + 1, ROAD_Y + 6): put(x0 + x, y, BODY)
        put(x0 + x, ROAD_Y + 6, BLUEGRAY)
        if (x & 3) == 1: put(x0 + x, ROAD_Y - 1, GRAY)        # 欄干
    if pillar:
        for y in range(ROAD_Y + 7, WALK):
            for x in range(2, 6): put(x0 + x, y, BLUEGRAY if x == 2 else BODY)
ROAD = strip('road', lambda kk: road(kk, False), ROAD_Y)
PILLAR = strip('road_pillar', lambda kk: road(kk, True), ROAD_Y)

def rubble(kk):
    x0 = kk * 8
    r = random.Random(kk)
    for x in range(8):
        h = r.choice([0, 1, 2, 2, 3, 4])
        for y in range(WALK - h, WALK): put(x0 + x, y, GRAY if y == WALK - h else BODY)
RUBBLE = strip('rubble', rubble)
LOWRUIN = [strip('lowruin', lambda kk: building(kk, 135, 'W', jag=8, holes=0.4), 140) for _ in range(2)]
while len(STRIPS) < 32: strip('spare', None)
assert len(STRIPS) == 32

# ★遠景(多重スクロール。遊んでの指摘): 予備の短冊 27..31 の 16..39 行(雲の底の下で、どのビルより上)に、遠い雲の筋を描く。
#   s1.c は短冊の表を 27, 28, 29, 30, 31, 27, 28, 29 にするので、筋は 1 本(8 ドット)の中に収める。地図の乱数は使わない
FAR_Y0, FAR_Y1 = 16, 40
fr = random.Random(1010)
for k in range(27, 32):
    x0 = k * 8
    for y in range(FAR_Y0, FAR_Y1):
        for x in range(8): put(x0 + x, y, SKY)
    for i in range(fr.randrange(1, 3)):
        n = fr.randrange(3, 9); cx = fr.randrange(0, 8 - n + 1); cy = fr.randrange(FAR_Y0 + 2, FAR_Y1 - 3)
        for x in range(cx, cx + n): put(x0 + x, cy, 4)
        for x in range(cx + 1, cx + n - 1): put(x0 + x, cy + 1, 3)

# ---- 地図 ----
cmap = []
turrets = []     # (列, 屋上の行)
def emit(k): cmap.append(k)

def lead(n):
    while len(cmap) < n:
        r = rnd.random()
        if r < 0.15: emit(LOWRUIN[rnd.randrange(2)]); emit(LOWRUIN[rnd.randrange(2)])
        elif r < 0.4: emit(RUBBLE)
        else: emit(EMPTY)
    del cmap[n:]

def city(n_cols, with_road):
    end = len(cmap) + n_cols
    nb = 0
    while len(cmap) < end:
        if with_road and rnd.random() < 0.45:
            for i in range(rnd.randrange(6, 14)):
                emit(PILLAR if i % 4 == 1 else ROAD)
            continue
        t = rnd.choice([40, 70, 70, 100, 100, 125])
        w = rnd.randrange(3, 8)
        start = len(cmap)
        if rnd.random() < 0.2 and t in (70, 100):
            for i in range(w): emit(RUIN[(0 if t == 70 else 2) + (i & 1)])
            roof = t + 6
        else:
            b = BLD[t]
            emit(b[0])
            for i in range(w - 2): emit(ANTENNA if (t == 40 and i == 1) else b[1 + (i & 1)])
            emit(b[3])
            roof = t
        nb += 1
        if nb % 3 == 0: turrets.append((start + w // 2, roof))
        for i in range(rnd.randrange(1, 4)): emit(RUBBLE if rnd.random() < 0.3 else EMPTY)
    del cmap[end:]

def plaza(n):
    for i in range(n): emit(RUBBLE if rnd.random() < 0.25 else EMPTY)

SEC = {}
SEC['DESCENT'] = len(cmap); lead(32)
SEC['CITY1'] = len(cmap); city(192, False)
SEC['ARENA1'] = len(cmap); plaza(40)
SEC['CITY2'] = len(cmap); city(160, True)
SEC['ARENA2'] = len(cmap); plaza(40)
SEC['ASCENT'] = len(cmap); plaza(64)
SEC['END'] = len(cmap)

def main(d, hpath):
    out = bytearray()
    for y in range(H):
        for x in range(0, W, 2): out.append((img[y][x] << 4) | img[y][x + 1])
    open(os.path.join(d, 'city_strips.bin'), 'wb').write(bytes(out))
    open(os.path.join(d, 'city_map.bin'), 'wb').write(bytes(cmap))
    with open(hpath, 'w') as f:
        f.write('/* city_data.h — tools/gen_city.py が生成(手で編集しない)。1 面の都市の区間と砲台の位置。 */\n')
        f.write('#ifndef CITY_DATA_H\n#define CITY_DATA_H\n\n')
        for k, v in SEC.items(): f.write(f'#define CITY_COL_{k} {v}\n')
        f.write(f'#define CITY_COLS {len(cmap)}\n#define CITY_ROAD_Y {ROAD_Y}\n#define CITY_WALK {WALK}\n\n')
        f.write(f'#define CITY_NTURRET {len(turrets)}\n#ifndef CITY_CLIMB\n')
        f.write('static const u16 city_turret_col[CITY_NTURRET] = {\n    ' +
                ', '.join(str(c) for c, _ in turrets) + ' };\n')
        f.write('static const u8 city_turret_row[CITY_NTURRET] = {\n    ' +
                ', '.join(str(r) for _, r in turrets) + ' };\n#endif\n')
        # ★Pawn が登るビルの左の壁の短冊 → 屋上の行(0 = 登れない)。pawn.c だけが使う(表を複製しないよう #ifdef で)
        climb = [0] * 32
        for t in HEIGHTS: climb[BLD[t][0]] = t
        f.write('\n#ifdef CITY_CLIMB\nstatic const u8 city_climb[32] = {\n    ' + ', '.join(str(c) for c in climb) + ' };\n#endif\n')
        f.write('\n#endif /* CITY_DATA_H */\n')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
