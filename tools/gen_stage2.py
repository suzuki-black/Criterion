#!/usr/bin/env python3
"""gen_stage2.py — 2 面「重力井戸」(指示書 5.2)の絵と地図を作る。外部の画像は使わず、手続きで描く。

  usage: gen_stage2.py <出力ディレクトリ> <s2_data.h>

1. s2_strips.bin: 短冊 32 本(8x188、4bpp。ページ 3 の 0..187 行)。軌道上の残骸の帯。
   地(色 1)は宇宙(ラスターで暗い藍)。天井(上)と床(下)に残骸の塊。高さは 4 段 × 4 段 = 16 通り × 2 柄。
   ★0..3 行は必ず色 6(表示リングの 188..191 行は描き替えないブロック。stage.c)。天井は最低 4 行。
   使う色: 1 宇宙 / 2 赤い灯(点滅)/ 3..5 瞬く星 / 6 残骸の影 / 7 縁の光 / 9 青灰の板 / 14 灰の板 / 15 星。
   2..5 は s2.c がパレットを回す(KILOGRAM の間は KILOGRAM の色。そのころには残骸の帯は無い)。
2. s2_map.bin: 地図(列ごとの短冊の番号、528 列)。区間ごとに天井と床の高さを変える。
3. s2_data.h: 区間の列の位置、短冊ごとの天井の行数と床の上端の行(自機の当たり)。
4. kg.bin: KILOGRAM(64x64、4bpp)。黒い球(色 2 / 3)、縁の光(4 / 5)、赤道の亀裂(7。KILOGRAM の間は残骸が無いので残骸の縁の色を借りる)。外は色 1。
   ゲームが ROTOR で縮小した絵を作り、近づいてくる様子にする。
"""
import math, os, random, sys

W, H = 256, 188
TOPS = [4, 14, 26, 40]                # 天井の行数(4 段)
BOTS = [0, 12, 26, 40]                # 床の行数
rnd = random.Random(2002)
img = [[1] * W for _ in range(H)]

def put(x, y, c):
    if 0 <= x < W and 0 <= y < H: img[y][x] = c

def wreck(x0, y0, y1, seed, face):
    """残骸の塊: 板の継ぎ目と鋲、端に縁の光。face = 1 は下向き(天井)、-1 は上向き(床)"""
    r = random.Random(seed)
    for y in range(y0, y1):
        for x in range(8):
            c = 6
            if (y // 6 + seed) % 3 == 0: c = 9
            if (y // 6 + seed) % 3 == 1 and x in (2, 3, 4, 5): c = 14
            if y % 6 == 0: c = 6
            put(x0 + x, y, c)
        if r.random() < 0.08: put(x0 + r.randrange(8), y, 2)   # ★赤い灯は色 2(s2.c が点滅させる)
    edge = y1 - 1 if face == 1 else y0
    for x in range(8): put(x0 + x, edge, 7)

STRIPS = []                           # (天井の行数, 床の上端の行)
for v in range(2):
    for ti, t in enumerate(TOPS):
        for bi, b in enumerate(BOTS):
            k = len(STRIPS)
            x0 = k * 8
            for y in range(4):
                for x in range(8): put(x0 + x, y, 6)     # ★0..3 行は色 6
            wreck(x0, 4, t, k * 7 + 1, 1) if t > 4 else None
            if b: wreck(x0, H - b, H, k * 7 + 3, -1)
            # 星(宇宙の部分にまばらに)
            for i in range(3 + v):
                y = rnd.randrange(t + 2, H - b - 2) if H - b - t > 6 else None
                if y is not None: put(x0 + rnd.randrange(8), y, 15 if rnd.random() < 0.4 else 3 + rnd.randrange(3))   # ★瞬く星(色 3..5)
            STRIPS.append((t, H - b))
assert len(STRIPS) == 32

# ★遠景(多重スクロール。遊んでの指摘): 地図で使わない短冊 11, 14, 15, 27, 30, 31 の 40..143 行(どの列も宇宙だけの行)に、遠い残骸と
#   星を描く。s2.c は短冊の表を 11, 14, 15, 27, 30, 31, 11, 14 にするので、絵は 1 本(8 ドット)の中に収め、境をまたがない。
#   ★地図の乱数(rnd)を進めないよう、別の乱数で描く
FAR = [11, 14, 15, 27, 30, 31]
FAR_Y0, FAR_Y1 = 40, 144
fr = random.Random(2020)
for k in FAR:
    x0 = k * 8
    for y in range(FAR_Y0, FAR_Y1):
        for x in range(8): img[y][x0 + x] = 1
    if fr.random() < 0.7:                                  # 遠い残骸(1 本に 0〜1 個、高さはばらばらに。暗い影 6 と、上の縁だけ青灰 9)
        w, h = fr.randrange(2, 7), fr.randrange(2, 6)
        cx, cy = fr.randrange(0, 8 - w + 1), fr.randrange(FAR_Y0 + 2, FAR_Y1 - 2 - h)
        for y in range(cy, cy + h):
            for x in range(cx, cx + w): img[y][x0 + x] = 9 if y == cy else 6
    for i in range(4):                                     # 遠い星(瞬く 3..5)
        img[fr.randrange(FAR_Y0 + 2, FAR_Y1 - 2)][x0 + fr.randrange(0, 8)] = 3 + fr.randrange(3)

def sid(ti, bi, v): return v * 16 + ti * 4 + bi

cmap = []
SEC = {}
def section(name, n, tlo, thi, blo, bhi):
    SEC[name] = len(cmap)
    ti = tlo; bi = blo
    for i in range(n):
        if i % 4 == 0:
            ti = max(tlo, min(thi, ti + rnd.choice([-1, 0, 1])))
            bi = max(blo, min(bhi, bi + rnd.choice([-1, 0, 1])))
        cmap.append(sid(ti, bi, rnd.randrange(2)))

section('SINK', 32, 0, 0, 0, 1)
section('DOWN', 128, 0, 1, 1, 3)
section('FLIP', 96, 1, 3, 0, 1)
section('ARENA1', 40, 0, 0, 1, 1)
section('CENTER', 128, 0, 2, 0, 2)
section('ARENA2', 40, 0, 0, 0, 0)
section('OUT', 64, 0, 0, 0, 0)
SEC['END'] = len(cmap)

def kilogram(n, R):
    """n x n の箱の真ん中に半径 R の球。R は実数(箱は 4 の倍数なので、球がどの大きさでも中心が揺れない)"""
    k = R / 30
    s = [[1] * n for _ in range(n)]
    cx = cy = (n - 1) / 2
    for y in range(n):
        for x in range(n):
            dx, dy = x - cx, y - cy
            d = math.hypot(dx, dy)
            if d > R: continue
            l = (-dx * 0.6 - dy * 0.5) / R                # 左上から光
            if d > R - max(2 * k, 1): c = 5                      # 縁取りは 1 色の輪(★左上と右下で色を替えていた間は、輪と本体がずれて見えた)
            elif abs(dy) < max(1.5 * k, 0.6) and abs(dx) < R - 4 * k: c = 7  # 赤道の亀裂(弱点)。★BG の色 7(13 はスプライトと共有)
            elif l > 0.55: c = 4
            elif l > 0.1: c = 3
            else: c = 2
            s[y][x] = c
    return s

# 近づく絵の列: 半径 7 → 45(戦う大きさ)を KG_STEP ずつ。1 枚 = 見出し 4 バイト(1 行のバイト数・行数・保つフレーム数・最後の印)+ 絵(zoom.c)
KG_R0, KG_R1, KG_STEP, KG_HOLD = 7.0, 45.0, 1.5, 13
def kg_stream():
    out = bytearray()
    n = int((KG_R1 - KG_R0) / KG_STEP + 0.5)
    for i in range(n + 1):
        R = KG_R0 + (KG_R1 - KG_R0) * i / n
        box = (int(2 * R + 2) + 3 + 2) // 4 * 4
        s = kilogram(box, R)
        out += bytes((box // 2, box, KG_HOLD, 1 if i == n else 0))
        for y in range(box):
            for x in range(0, box, 2): out.append((s[y][x] << 4) | s[y][x + 1])
    return bytes(out)

def main(d, hpath):
    out = bytearray()
    for y in range(H):
        for x in range(0, W, 2): out.append((img[y][x] << 4) | img[y][x + 1])
    open(os.path.join(d, 's2_strips.bin'), 'wb').write(bytes(out))
    open(os.path.join(d, 's2_map.bin'), 'wb').write(bytes(cmap))
    open(os.path.join(d, 'kg.bin'), 'wb').write(kg_stream())
    with open(hpath, 'w') as f:
        f.write('/* s2_data.h — tools/gen_stage2.py が生成(手で編集しない)。2 面の区間と短冊の当たり。 */\n')
        f.write('#ifndef S2_DATA_H\n#define S2_DATA_H\n\n')
        for k, v in SEC.items(): f.write(f'#define S2_COL_{k} {v}\n')
        f.write(f'#define S2_COLS {len(cmap)}\n\n')
        f.write('static const u8 s2_top[32] = { ' + ', '.join(str(t) for t, _ in STRIPS) + ' };   /* 天井の行数 */\n')
        f.write('static const u8 s2_bot[32] = { ' + ', '.join(str(b) for _, b in STRIPS) + ' };   /* 床の上端の行 */\n')
        f.write('\n#endif /* S2_DATA_H */\n')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
