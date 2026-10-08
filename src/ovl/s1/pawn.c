/* pawn.c — Pawn(4x4 ドットの機械兵)をスプライトを使わず BG に直接描く(指示書 5.1 の主役の技)。
   ・絵はページ 2 の 200..207 行(pawn_sheet_load。中身は sheet[] のコメント)。地の色ごとの版を持つ。
   ・街路の Pawn(都市の前半・後半、最大 16 体): 世界の座標で 2 本の車線(s = 160 / 176。173 行の車線の破線と
     158 行の歩道は踏まない)を 16 フレームに 1 回 2 ドット歩く(ちまちま行進)。★6x8 の白い体に黄色の頭とオレンジの目、
     足を動かす 2 コマ(4x4 の灰色では黒い街路で見つけられなかった。M7 の後の指摘 A4)。LEGION のマスは 4x4 のまま。
     ★描き込みは 1 フレームに 1 コマンドまで。VDP がコマンドを実行している間は CPU の VRAM の書き込み
       (スプライトの属性表・HUD)も遅くなり、24 体を 8 フレームごとに歩かせたら 1 フレームが 90 ライン延びた余白つきの 8x4 を HMMM で写すので、1 回で
     前の位置の消しも済む(黒地の街路だから。指示書の「黒基調規約」)。表示リングの x = 世界の x & 511。
   ・撃たれると消して爆発、ときどき上の自機を狙って撃つ。
   ★表示リングへの描き込みは GHOST の待ち行列(ghost.c)。1 フレームに出す数は呼ぶ側が抑える。 */
#include "s1.h"
#include "vdp.h"
#include "ghost.h"

#define SHEET_Y  (512u + 200u)
#define NPW      16
#define LANES    2

static const u8 lane_s[LANES] = { 160, 176 };
#define WALK_X   48              /* 街路の Pawn の 2 コマ(10x8、左右 2 ドットの余白つき)のシートの x */

/* 8 行 × 24 バイト(48 ドット)。0 = 黒、1 = 空、14 = 灰、13 = 赤い目。
   上 4 行: x 0 = 空の地の A / 4 = B / 8 = 空 / 16, 24 = 使わない(旧の 4x4 の街路の Pawn。街路は walk[])
   x 32..47 は 4x8(粒子が 1 マス動くとき、動いた先と描き戻す元のマスを 1 回で写す):
     32 = 上 A・下 空(空へ登る)/ 36 = 上 A・下 A(積んだ所を通る)/ 40 = 上 空・下 A(空から落ちる) */
static const u8 sheet[8][24] = {
    { 0x1E, 0xE1, 0x1E, 0xE1, 0x11, 0x11, 0x00, 0x00, 0x00, 0x0E, 0xE0, 0x00, 0x00, 0x0E, 0xE0, 0x00, 0x1E, 0xE1, 0x1E, 0xE1, 0x11, 0x11, 0x00, 0x00 },
    { 0xED, 0xDE, 0xED, 0xDE, 0x11, 0x11, 0x00, 0x00, 0x00, 0xED, 0xDE, 0x00, 0x00, 0xED, 0xDE, 0x00, 0xED, 0xDE, 0xED, 0xDE, 0x11, 0x11, 0x00, 0x00 },
    { 0xEE, 0xEE, 0xEE, 0xEE, 0x11, 0x11, 0x00, 0x00, 0x00, 0xEE, 0xEE, 0x00, 0x00, 0xEE, 0xEE, 0x00, 0xEE, 0xEE, 0xEE, 0xEE, 0x11, 0x11, 0x00, 0x00 },
    { 0xE1, 0x1E, 0x1E, 0xE1, 0x11, 0x11, 0x00, 0x00, 0x00, 0xE0, 0x0E, 0x00, 0x00, 0x0E, 0xE0, 0x00, 0xE1, 0x1E, 0xE1, 0x1E, 0x11, 0x11, 0x00, 0x00 },
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x1E, 0xE1, 0x1E, 0xE1, 0x00, 0x00 },
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0xED, 0xDE, 0xED, 0xDE, 0x00, 0x00 },
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0xEE, 0xEE, 0xEE, 0xEE, 0x00, 0x00 },
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0xE1, 0x1E, 0xE1, 0x1E, 0x00, 0x00 },
};

static const u8 walk[8][10] = {
    { 0x00, 0x00, 0xCC, 0x00, 0x00, 0x00, 0x00, 0xCC, 0x00, 0x00 },
    { 0x00, 0x0F, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xE0, 0x00 },
    { 0x00, 0x0F, 0xDD, 0xE0, 0x00, 0x00, 0x0F, 0xDD, 0xE0, 0x00 },
    { 0x00, 0x0F, 0xFF, 0xE0, 0x00, 0x00, 0x0F, 0xFF, 0xE0, 0x00 },
    { 0x00, 0xF0, 0xFE, 0x0F, 0x00, 0x00, 0x0F, 0xFE, 0xE0, 0x00 },
    { 0x00, 0x00, 0xFE, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00 },
    { 0x00, 0x0F, 0x00, 0xE0, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00 },
    { 0x00, 0xF0, 0x00, 0x0E, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00 },
};

void pawn_sheet_load(void) {
    u8 r;
    for (r = 0; r < 8; r++) {
        vdp_write_addr17(1, (u16)((200u + r) * 128u));
        vdp_out_block(24, sheet[r]);
        vdp_write_addr17(1, (u16)((200u + r) * 128u + WALK_X / 2));
        vdp_out_block(10, walk[r]);
    }
}

/* 4x8(x, y = 上のマス): kind 0 = 上 A・下 空、1 = 上 A・下 A、2 = 上 空・下 A */
void pawn_move(s16 x, s16 y, u8 kind) {
    ring_cmd((u16)(32u + kind * 4u), SHEET_Y, x, y, 4, 8, 0, 0);
}

/* 空の地の 4x4: kind 0 = A、1 = B、2 = 空(消す) */
void pawn_cell(s16 x, s16 y, u8 kind) {
    ring_cmd((u16)(kind * 4u), SHEET_Y, x, y, 4, 4, 0, 0);
}

/* ---- 街路の Pawn ---- */
typedef struct { u16 wx; u8 lane; s8 dir; u8 on; } Pw;
static Pw pw[NPW];
static u8 tick, group_t, alive, half;

void pawn_reset(void) {
    u8 i;
    for (i = 0; i < NPW; i++) pw[i].on = 0;
    tick = 0; group_t = 0; alive = 0; half = 0;   /* ★half も戻す(オーバレイの変数は初期化されない。254/255 で交互になり、
                                                     更新が毎回途中で抜けて Pawn が一度も出ていなかった) */
}

/* 画面の右の外に 4〜7 体の隊列を出す */
static void spawn_group(void) {
    u8 i, n = (u8)(4 + (rnd8() & 3)), lane = (u8)(rnd8() & 1), k = 0;
    s8 dir = (rnd8() & 1) ? 1 : -1;
    for (i = 0; i < NPW && k < n; i++) {
        if (pw[i].on) continue;
        pw[i].on = 1; pw[i].lane = lane; pw[i].dir = dir;
        pw[i].wx = (u16)((g_wx + 264u + k * 10u) & ~1u);
        k++;
    }
}

static void kill(Pw *p) {
    s16 x = (s16)(p->wx - g_wx);
    p->on = 0;
    ring_cmd(0, 0, (s16)(x - 2), (s16)(lane_s[p->lane] - g_scam), 10, 8, 1, 0x00);   /* 黒で消す */
    fx_spawn(FX(x + 3), FX(lane_s[p->lane] - g_scam + 4));
    score_add(2);
}

/* 都市の区間で毎フレーム(stage.c が呼ぶ)。active = 0 なら新しく出さない。
   ★1 フレームに見るのは 16 体に 1 体(歩く・画面の外を消す)。ショットの当たりは、街路の高さを飛んでいる
     ショットがあるときだけ全員を見る(32 体 × 12 発を毎フレーム比べると 100 ライン使った) */
void pawn_update(u8 active) {
    u8 i, k, ns = 0, sx[MAX_PSHOT], sy[MAX_PSHOT], si[MAX_PSHOT];
    s16 top = (s16)(lane_s[0] - g_scam - 4);
    /* ★2 フレームに 1 回(1 回 10 ライン強。都市の重いフレームと重なって遅れた)。ショットは 1 回に 16 ドット
       進むので、当たりの幅を後ろへ 16 ドット広げる */
    if ((half ^= 1)) return;
    tick++;
    if (active && ++group_t >= 45) { group_t = 0; spawn_group(); }   /* 2 フレームに 1 回なので 1.5 秒ごと */
    if (g_lb) {                                               /* LIMIT BREAK: 画面の中の Pawn を 1 回に 3 体ずつ(消すのは VDP の
                                                                 コマンドなので、一度に全員だと重い) */
        u8 n = 3;
        for (i = 0; i < NPW && n; i++) {
            Pw *p = &pw[i];
            if (p->on && (u16)(p->wx - g_wx) <= 251) { kill(p); n--; }
        }
    }
    for (k = 0; k < MAX_PSHOT; k++) {                          /* 街路の高さのショット */
        const Bullet *b = &g_pshot[k];
        s16 x, y;
        if (!b->on) continue;
        y = PX(b->y); x = PX(b->x);
        if (y < top || y > PLAY_H || x < 0 || x > 255) continue;
        sx[ns] = (u8)x; sy[ns] = (u8)y; si[ns] = k; ns++;
    }
    if (ns) {
        /* 車線ごとに、画面の中の Pawn の x(8 ビット)を並べてから、ショットはその車線だけ見る
           (全員 × 全ショットだと、自機が街路の高さを飛ぶとき 30 ライン使った) */
        u8 lx[LANES][NPW], li[LANES][NPW], ln[LANES] = { 0, 0 };
        for (i = 0; i < NPW; i++) {
            Pw *p = &pw[i];
            u16 x;
            u8 l;
            if (!p->on) continue;
            x = (u16)(p->wx - g_wx);
            if (x > 251) continue;
            l = p->lane;
            if (ln[l] < NPW) { lx[l][ln[l]] = (u8)x; li[l][ln[l]] = i; ln[l]++; }
        }
        for (k = 0; k < ns; k++) {
            u8 l, m, y = sy[k];
            s16 ly = (s16)(lane_s[0] - g_scam);
            for (l = 0; l < LANES; l++) if ((u8)(y - (u8)(ly + lane_s[l] - lane_s[0]) + 4) < 12) break;
            if (l == LANES) continue;
            for (m = 0; m < ln[l]; m++) {
                Pw *p = &pw[li[l][m]];
                if (!p->on || (u8)(sx[k] - lx[l][m] + 6) >= 28) continue;   /* 前の判定から 16 ドット進んだ分も */
                { Bullet *b = &g_pshot[si[k]]; if (!(b->pierce | (b->on & 2))) b->on = 0; }   /* DROP の爆雷(on 2 / 3)は消えずに列ごと吹き飛ばす */
                kill(p);
                break;
            }
        }
    }
    for (i = (u8)(tick & 15); i < NPW; i += 16) {             /* 16 体に 1 体(2 フレームに 1 体): 歩く */
        Pw *p = &pw[i];
        s16 x;
        if (!p->on) continue;
        x = (s16)(p->wx - g_wx);
        if (x < -8 || x > 300) { p->on = 0; continue; }       /* 画面の外(リングの列は流れで描き直される) */
        if (x >= 0 && x < 256 + 8 && gq_free() >= 4) {          /* 待ち行列に空きがあるときだけ歩く */
            p->wx += (u16)(s16)(p->dir * 2);
            ring_cmd((u16)(((tick >> 4) & 1) ? WALK_X + 10 : WALK_X), SHEET_Y, (s16)(x + p->dir * 2 - 2),
                     (s16)(lane_s[p->lane] - g_scam), 10, 8, 0, 0);
        }
    }
    {                                                         /* 1 フレームに 1 体だけ、撃つかを決める */
        Pw *p = &pw[(u8)(tick % NPW)];
        if (p->on && rnd8() < 3) {                                /* 1 秒に約 0.7 発 */
            s16 x = (s16)(p->wx - g_wx);
            if (x > 32 && x < 224) enemy_shoot(FX(x + 3), FX(lane_s[p->lane] - g_scam));
        }
    }
}
