#!/usr/bin/env python3
"""gen_hud.py — HUD 帯の∞ゲージ(レムニスケート・ゲージ、指示書 2.3)の絵と座標を C ヘッダにする。

  usage: gen_hud.py <out.h>

- 軌道: ベルヌーイのレムニスケート x = a cos t / (1 + sin^2 t), y = a sin t cos t / (1 + sin^2 t)(縦は縮める)。
- カーソルの巡回順(指示書 2.3): ∞ → THRUST → ECHO → AEGIS → ∞ → DROP → SPLIT → RAY → ∞。
  軌道を中心から左上へ回り始め、t を π/4 ずつ進めた点にノードを置く(左のループ = 機体系、右 = 兵装系)。
  ★指示書の図では左端が AEGIS・左下が ECHO だが、巡回順を軌道に当てはめると左端が ECHO・左下が AEGIS になる。
    巡回順(規則)を優先した。
- 出力: hud_gauge[24][64](HUD 帯 24 行 × 左 128 ドット、4bpp)、gauge_node[8][2](ノードの中心)、
        gauge_path[8][PATH_STEPS][2](ノード i から i+1 へ向かうカーソルの中心の道のり)。
"""
import math, sys

W, H = 128, 24
CX, CY = 64, 11.5          # HUD 帯の中の中心(画面では 188 + CY 行)
A = 52.0
YS = 0.46                  # 縦の縮め
PATH_STEPS = 8
C_PATH, C_NODE, C_CENTER, C_LABEL = 14, 12, 15, 10

# ノード: カーソルの番号 → t
T0 = math.pi / 2
NODE_T = [T0 + k * math.pi / 4 for k in range(8)]
NAMES = ['INF', 'THRUST', 'ECHO', 'AEGIS', 'INF', 'DROP', 'SPLIT', 'RAY']
LETTER = {'THRUST': 'T', 'ECHO': 'E', 'AEGIS': 'A', 'DROP': 'D', 'SPLIT': 'S', 'RAY': 'R'}

FONT = {   # 3x5
    'T': ['###', '.#.', '.#.', '.#.', '.#.'],
    'E': ['###', '#..', '##.', '#..', '###'],
    'A': ['.#.', '#.#', '###', '#.#', '#.#'],
    'D': ['##.', '#.#', '#.#', '#.#', '##.'],
    'S': ['.##', '#..', '.#.', '..#', '##.'],
    'R': ['##.', '#.#', '##.', '#.#', '#.#'],
}

def pt(t):
    s, c = math.sin(t), math.cos(t)
    d = 1 + s * s
    return CX + A * c / d, CY + A * s * c / d * YS

def main(path):
    img = [[0] * W for _ in range(H)]
    def put(x, y, c):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < W and 0 <= y < H: img[y][x] = c
    for i in range(1200):
        x, y = pt(i * 2 * math.pi / 1200)
        put(x, y, C_PATH)
    nodes = [pt(t) for t in NODE_T]
    for i, (x, y) in enumerate(nodes):
        c = C_CENTER if NAMES[i] == 'INF' else C_NODE
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                put(x + dx, y + dy, c)
        name = NAMES[i]
        if name in LETTER:
            # 頭文字はノードの横(左のループは左側、右のループは右側)
            lx = x - 6 if x < CX else x + 3
            ly = y - 2
            for r, row in enumerate(FONT[LETTER[name]]):
                for k, ch in enumerate(row):
                    if ch == '#': put(lx + k, ly + r, C_LABEL)
    paths = []
    for i in range(8):
        t0, t1 = NODE_T[i], NODE_T[i] + math.pi / 4
        paths.append([pt(t0 + (t1 - t0) * (k + 1) / PATH_STEPS) for k in range(PATH_STEPS)])
    with open(path, 'w') as f:
        f.write('/* hud_data.h — tools/gen_hud.py が生成(手で編集しない)。∞ゲージの絵と座標。 */\n')
        f.write('#ifndef HUD_DATA_H\n#define HUD_DATA_H\n\n')
        f.write(f'#define GAUGE_W {W}\n#define GAUGE_H {H}\n#define PATH_STEPS {PATH_STEPS}\n\n')
        f.write('static const u8 hud_gauge[GAUGE_H][GAUGE_W / 2] = {\n')
        for row in img:
            f.write('    { ' + ', '.join(f'0x{(row[x] << 4) | row[x + 1]:02X}' for x in range(0, W, 2)) + ' },\n')
        f.write('};\n\n/* ノードの中心(HUD 帯の中の x, y)。番号はカーソルの巡回順(0 と 4 が ∞) */\n')
        f.write('static const u8 gauge_node[8][2] = {\n')
        for (x, y), n in zip(nodes, NAMES):
            f.write(f'    {{ {int(round(x))}, {int(round(y))} }},  /* {n} */\n')
        f.write('};\n\n/* ノード i から i+1 へ向かうカーソルの中心 */\n')
        f.write('static const u8 gauge_path[8][PATH_STEPS][2] = {\n')
        for p in paths:
            f.write('    { ' + ', '.join(f'{{ {int(round(x))}, {int(round(y))} }}' for x, y in p) + ' },\n')
        f.write('};\n\n#endif /* HUD_DATA_H */\n')

if __name__ == '__main__':
    main(sys.argv[1])
