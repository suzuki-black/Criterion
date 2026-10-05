#!/usr/bin/env python3
"""gen_stage3.py — 3 面「臨界加速」(指示書 5.3)の絵と地図を作る。外部の画像は使わず、手続きで描く。

  usage: gen_stage3.py <出力ディレクトリ> <s3_data.h>

1. s3_strips.bin: 短冊 32 本(8x188、4bpp)。
     0..3   月面(上は星空、下 140 行から灰の地面とクレーター)
     4..5   チューブの入口(大きな壁と開口)
     6..9   チューブの中(上の壁 0..15 行、下の壁 172..187 行、壁の面に照明パネル)。★通り道を 16..171 行に広げた
            (28..159 では高速で避けきれなかった。M7 の後)
     10..13 その流線版(パネルが線になる。高速のとき 6..9 をこれに置き換える)
     14 / 15 照明なし / その流線版
     16..18 隔壁(すき間が上 / 中 / 下)
     19     隔壁(閉じている)
   ★0..3 行は必ず色 6(stage.c のブロック 23)。3 面は色 6 を暗い灰にする。
   使う色: 1 内部の暗がり(ラスター)/ 2..5 流れる照明(s3.c がパレットを回す)/ 6 壁の影 / 7 縁 / 9 青灰 / 12 黄 /
   13 危険の縞(赤)/ 14 灰 / 15 白・星。
2. s3_map.bin: 地図 640 列(月面 → 入口 → 加速 → 停止(閉じた隔壁 2 枚)→ 最高速(隔壁が並ぶ)→ ボスの広場)。
3. s3_data.h: 区間の列、短冊ごとの上下の限り(自機の当たり)、隔壁の列の一覧。
4. tacho.bin: 回転計の盤(128x128、4bpp)。目盛りとレッドゾーン。数字はスプライト(壊せる)なので描かない。
"""
import math, os, random, sys

W, H = 256, 188
rnd = random.Random(3003)
img = [[1] * W for _ in range(H)]
LIMITS = []                            # (上の限り, 下の限り): 自機が居られる s の範囲

def put(x, y, c):
    if 0 <= x < W and 0 <= y < H: img[y][x] = c

def base_top(x0):
    for y in range(4):
        for x in range(8): put(x0 + x, y, 6)

def lunar(k):
    x0 = k * 8; base_top(x0)
    r = random.Random(k)
    for y in range(4, 140):
        if r.random() < 0.03: put(x0 + r.randrange(8), y, 15 if r.random() < 0.5 else 14)
    for x in range(8):
        top = 140 + int(3 * math.sin((x0 + x) / 9.0) + 2 * math.sin((x0 + x) / 3.7))
        for y in range(top, H):
            c = 14 if y < top + 2 else (6 if (x + y) % 5 == 0 else 9)
            put(x0 + x, y, c)
    LIMITS.append((0, 134))

def wall_band(x0, y0, y1, k, lights, streak, lc=15):
    for y in range(y0, y1):
        for x in range(8):
            c = 9 if (y // 4) % 2 else 6
            if y in (y0, y1 - 1): c = 7
            put(x0 + x, y, c)
    for ly in lights:
        for x in range(8):
            if streak or x in (2, 3, 4, 5):
                put(x0 + x, ly, lc); put(x0 + x, ly + 1, lc)

def tube(k, lit, streak, lc=15):
    x0 = k * 8; base_top(x0)
    wall_band(x0, 4, 16, k, [10] if lit else [], streak, lc)
    wall_band(x0, 172, H, k, [178] if lit else [], streak, lc)
    LIMITS.append((16, 156))

def mouth(k):
    x0 = k * 8; base_top(x0)
    for y in range(4, H):
        if 40 <= y < 148: continue
        for x in range(8): put(x0 + x, y, 14 if (y + x) % 7 else 13)
    LIMITS.append((40, 132))

def bulkhead(k, gap):
    x0 = k * 8; base_top(x0)
    wall_band(x0, 4, 16, k, [], False)
    wall_band(x0, 172, H, k, [], False)
    for y in range(16, 172):
        if gap and gap[0] <= y < gap[1]: continue
        for x in range(8):
            c = 13 if ((y + x) // 4) % 2 else 14
            if x in (0, 7): c = 7
            put(x0 + x, y, c)
    LIMITS.append((gap[0], gap[1] - 16) if gap else (16, 16))

STRIPS = []
for k in range(4): lunar(k)
mouth(4); mouth(5)
# ★照明は色 2..5 を列ごとに順に使い、s3.c がパレットを回して壁を流れる光にする(M7 の後)
for k in range(6, 10): tube(k, True, False, 2 + (k - 6))
for k in range(10, 14): tube(k, True, True, 2 + (k - 10))
tube(14, False, False); tube(15, False, True)
bulkhead(16, (16, 68)); bulkhead(17, (68, 120)); bulkhead(18, (120, 172)); bulkhead(19, None)
for k in range(20, 32): tube(k, False, False)
assert len(LIMITS) == 32

cmap = []
SEC = {}
BULK = []
def tubecols(n, bulk_every=0):
    for i in range(n):
        if bulk_every and i % bulk_every == bulk_every - 1:
            BULK.append(len(cmap)); cmap.append(16 + rnd.randrange(3))
        else:
            cmap.append(6 + (len(cmap) & 3))          # 照明の色 2..5 を列の順に(流れる光)

SEC['LUNAR'] = 0
for i in range(40): cmap.append(rnd.randrange(4))
SEC['MOUTH'] = len(cmap)
cmap += [4] * 4 + [5] * 4
SEC['ACCEL'] = len(cmap); tubecols(256, 48)
SEC['STOP'] = len(cmap)
cmap += [14, 14, 19, 14, 14, 14, 19, 14]
SEC['TOP'] = len(cmap); tubecols(256, 24)          # ボスの広場は 72 列(突進で 20 列ほど前へ出る)
SEC['ARENA'] = len(cmap)
cmap += [14] * (640 - len(cmap))
SEC['END'] = len(cmap)
assert len(cmap) == 640

def tacho():
    N = 128
    s = [[1] * N for _ in range(N)]
    c = 63.5
    for y in range(N):
        for x in range(N):
            dx, dy = x - c, y - c
            d = math.hypot(dx, dy)
            if d > 62: continue
            a = (math.degrees(math.atan2(-dy, dx)) + 360) % 360       # 0 = 右、反時計回り
            col = 6 if d > 58 else 9 if d > 56 else 1
            if 44 < d < 54:
                sweep = (225 - a) % 360                              # 225 度(左下)から時計回り
                if sweep <= 270:
                    if sweep > 225: col = 13                         # レッドゾーン
                    elif int(sweep) % 27 < 2: col = 15               # 太い目盛り
                    elif int(sweep) % 9 < 1 and d > 49: col = 14
            if d < 6: col = 14
            s[y][x] = col
    out = bytearray()
    for y in range(N):
        for x in range(0, N, 2): out.append((s[y][x] << 4) | s[y][x + 1])
    return bytes(out)

def main(d, hpath):
    out = bytearray()
    for y in range(H):
        for x in range(0, W, 2): out.append((img[y][x] << 4) | img[y][x + 1])
    open(os.path.join(d, 's3_strips.bin'), 'wb').write(bytes(out))
    open(os.path.join(d, 's3_map.bin'), 'wb').write(bytes(cmap))
    open(os.path.join(d, 'tacho.bin'), 'wb').write(tacho())
    with open(hpath, 'w') as f:
        f.write('/* s3_data.h — tools/gen_stage3.py が生成(手で編集しない)。3 面の区間と短冊の当たり。 */\n')
        f.write('#ifndef S3_DATA_H\n#define S3_DATA_H\n\n')
        for k, v in SEC.items(): f.write(f'#define S3_COL_{k} {v}\n')
        f.write(f'#define S3_COLS {len(cmap)}\n#define S3_NBULK {len(BULK)}\n\n')
        f.write('static const u8 s3_ymin[32] = { ' + ', '.join(str(a) for a, _ in LIMITS) + ' };\n')
        f.write('static const u8 s3_ymax[32] = { ' + ', '.join(str(b) for _, b in LIMITS) + ' };\n')
        f.write('static const u16 s3_bulk[S3_NBULK] = { ' + ', '.join(str(b) for b in BULK) + ' };   /* 隔壁の列(開閉する) */\n')
        f.write('\n#endif /* S3_DATA_H */\n')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
