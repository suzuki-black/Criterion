/* boss.c — 1 面の中ボス LEGION(仮)とボス PLUMB(仮)(指示書 5.1)。
   ★仮: 本物の LEGION は BG に直接描く Pawn の群体(粒子)、PLUMB は ROTOR によるリアルタイム回転で、M3。
     ここではスプライトで同じ遊び(何を撃てば崩せるか、どう動くか)を先に作り、M2 の流れ(スクロールが止まる・
     倒すと先へ進む)を確かめる。
   ・LEGION(仮): 9 体の Pawn が人型に積み上がり、胸に核(隊長機)。撃たれた Pawn は剥がれて地面に落ち、
     しばらくすると這い上がって穴を埋める。核を壊すと全体が崩れ、Pawn が四方へ逃げ散る。
   ・PLUMB(仮): 上空から鎖(6 節)で吊られた 32x32 の下げ振り。振り子で左右に揺れ、先端から自機を狙う。
     体力が半分を切ると鎖が切れ、地面を跳ね回る。上部の吊り輪は 2 倍の傷。
   座標はドット(中心)。画面の外の上は y < 0。 */
#include "game.h"
#include "spr.h"
#include "sprite_data.h"

#define GROUND   152               /* 都市の歩道(158)の少し上 */
#define LEGION_HP 40
#define PLUMB_HP  70
#define PLUMB_HALF 35

u8 g_boss_state;
u8 g_boss_hp;
static u8  kind;
static u16 t;
static s16 bx, by;

/* ---- LEGION(仮) ---- */
#define NPAWN 9
static const s8 pawn_dx[NPAWN] = { 0, -14, 14, -24, 24, -8, 8, -10, 10 };   /* 頭・肩・腕・腰・脚 */
static const s8 pawn_dy[NPAWN] = { -34, -20, -20, -6, -6, 6, 6, 22, 22 };
static u8  pawn_off[NPAWN];        /* 0 = 体にいる / それ以外 = 剥がれてからのフレーム */
static u8  pawn_hp[NPAWN];
static s16 pawn_y[NPAWN], pawn_vy[NPAWN];   /* 剥がれている Pawn(固定小数点) */
static s16 pawn_x[NPAWN], pawn_vx[NPAWN];   /* 崩れて逃げ散るとき */
static u8  spread_t, aim_t;        /* 撃つ間隔(割り算・余りを毎フレーム使わない) */

/* ---- PLUMB(仮) ---- */
#define CHAIN_N 6
#define CHAIN_L 96
static s16 anchor_y;
static u8  broken;
static s16 fx, fy, fvx, fvy;       /* 鎖が切れてから(固定小数点) */

void boss_start(u8 k) {
    u8 i;
    kind = k; t = 0; g_boss_state = BS_ENTER;
    if (k == BOSS_LEGION) {
        g_boss_hp = LEGION_HP; bx = 300; by = 118; spread_t = 0;
        for (i = 0; i < NPAWN; i++) { pawn_off[i] = 0; pawn_hp[i] = 2; }
    } else {
        g_boss_hp = PLUMB_HP; bx = 128; anchor_y = -110; broken = 0; aim_t = 0;
    }
}

void boss_leave(void) { if (g_boss_state != BS_NONE) g_boss_state = BS_DONE; }

void boss_damage(u8 n) {
    if (g_boss_state != BS_FIGHT) return;
    if (g_boss_hp > n) { g_boss_hp -= n; return; }
    g_boss_hp = 0; g_boss_state = BS_DYING; t = 0;
    score_add(kind == BOSS_LEGION ? 200 : 500);
    if (kind == BOSS_LEGION) {
        u8 i;
        for (i = 0; i < NPAWN; i++) {                         /* 崩落: 四方へ逃げ散る */
            pawn_x[i] = FX(bx + pawn_dx[i]);
            pawn_y[i] = FX(by + pawn_dy[i]);
            pawn_vx[i] = (s16)((s8)(rnd8() & 63) - 32);
            pawn_vy[i] = (s16)(-(s16)(rnd8() & 31) - 16);
            pawn_off[i] = 0;
        }
    }
}

/* 自機のショット: 毎フレーム最初に画面座標(8 ビット)の表にしておき、部位ごとの判定は 8 ビットの比較だけにする
   (固定小数点のまま 10 部位 × 12 発を比べると、1 フレームに収まらなかった) */
static u8 shx[MAX_PSHOT], shy[MAX_PSHOT], shi[MAX_PSHOT], nsh;

static void shots_prepare(void) {
    u8 i;
    nsh = 0;
    for (i = 0; i < MAX_PSHOT; i++) {
        const Bullet *b = &g_pshot[i];
        s16 x, y;
        if (!b->on) continue;
        x = PX(b->x); y = PX(b->y);
        if (x < 0 || x > 255 || y < 0 || y > 255) continue;
        shx[nsh] = (u8)x; shy[nsh] = (u8)y; shi[nsh] = i; nsh++;
    }
}

/* (x, y) から半径 r(ドット)に当たった弾の数。当たった弾は消える(貫通は残る) */
static u8 shot_hit(s16 x, s16 y, u8 r) {
    u8 k, n = 0, r2 = (u8)(r * 2), ux, uy;
    if (x < 0 || x > 255 || y < 0 || y > 255) return 0;
    ux = (u8)(x - r); uy = (u8)(y - r);
    for (k = 0; k < nsh; k++) {
        Bullet *b;
        if ((u8)(shx[k] - ux) >= r2 || (u8)(shy[k] - uy) >= r2) continue;
        b = &g_pshot[shi[k]];
        if (!b->on) continue;
        if (!b->pierce) b->on = 0;
        n++;
    }
    return n;
}

static void touch(s16 x, s16 y, u8 r) {
    s16 dx, dy;
    if (!g_pl.alive) return;
    dx = PX(g_pl.x) + 8 - x; dy = PX(g_pl.y) + 8 - y;
    if (dx > -(s16)r && dx < (s16)r && dy > -(s16)r && dy < (s16)r) player_hit();
}

/* 5 方向(左向き 2 ドット/フレーム) */
static void spread5(s16 x, s16 y) {
    enemy_bullet(FX(x), FX(y), -32, 0);
    enemy_bullet(FX(x), FX(y), -30, 10);  enemy_bullet(FX(x), FX(y), -30, -10);
    enemy_bullet(FX(x), FX(y), -26, 18);  enemy_bullet(FX(x), FX(y), -26, -18);
}

static void legion_update(void) {
    u8 i;
    if (g_boss_state == BS_ENTER) {
        bx -= 2;
        if (bx <= 196) g_boss_state = BS_FIGHT;
        return;
    }
    if (g_boss_state == BS_DYING) {
        for (i = 0; i < NPAWN; i++) { pawn_x[i] += pawn_vx[i]; pawn_y[i] += pawn_vy[i]; pawn_vy[i] += 2; }
        if (!(t & 7)) fx_spawn(FX(bx - 16 + (rnd8() & 31)), FX(by - 30 + (rnd8() & 63)));
        if (t >= 90) g_boss_state = BS_DONE;
        return;
    }
    /* 揺れながら左右に歩く */
    bx = (s16)(176 + (g_sin64[(u8)(t >> 2) & 63] >> 3));
    by = (s16)(118 + (g_sin64[(u8)(t >> 1) & 63] >> 6));
    for (i = 0; i < NPAWN; i++) {
        s16 x = bx + pawn_dx[i], y = by + pawn_dy[i];
        if (pawn_off[i]) {                                    /* 剥がれている: 落ちて、待って、這い上がる */
            pawn_off[i]++;
            if (pawn_off[i] < 60) {
                pawn_y[i] += pawn_vy[i]; pawn_vy[i] += 3;
                if (pawn_y[i] > FX(GROUND)) { pawn_y[i] = FX(GROUND); pawn_vy[i] = 0; }
            } else if (pawn_off[i] < 150) {
                s16 ty = FX(y);
                pawn_y[i] += (s16)((ty - pawn_y[i]) >> 4);   /* 這い上がる */
            } else { pawn_off[i] = 0; pawn_hp[i] = 2; }
            continue;
        }
        if (shot_hit(x, y, 7) && --pawn_hp[i] == 0) {
            pawn_off[i] = 1; pawn_y[i] = FX(y); pawn_vy[i] = -16;
            fx_spawn(FX(x), FX(y)); score_add(1);
        }
        touch(x, y, 8);
    }
    {
        s16 cx = bx, cy = (s16)(by - 12);
        u8 n = shot_hit(cx, cy, 8);
        while (n--) boss_damage(1);
        touch(cx, cy, 8);
        if ((t & 63) == 32) {                                 /* 腕の Pawn が狙って撃つ */
            if (!pawn_off[3]) enemy_shoot(FX(bx + pawn_dx[3]), FX(by + pawn_dy[3]));
            if (!pawn_off[4]) enemy_shoot(FX(bx + pawn_dx[4]), FX(by + pawn_dy[4]));
        }
        if (++spread_t >= 150) { spread_t = 0; spread5(cx, cy); }
    }
}

static void plumb_pos(void) {
    s8 a = (s8)(g_sin64[(u8)(t >> 1) & 63] >> 4);           /* 振れ角 ±7(64 で一周)≒ ±40 度 */
    s16 sx = g_sin64[(u8)a & 63], cx = g_sin64[(u8)(a + 16) & 63];
    bx = (s16)(128 + ((sx * CHAIN_L) >> 7));
    by = (s16)(anchor_y + ((cx * CHAIN_L) >> 7));
}

static void plumb_update(void) {
    if (g_boss_state == BS_ENTER) {
        anchor_y += 2;
        if (anchor_y >= -8) { anchor_y = -8; g_boss_state = BS_FIGHT; }
        plumb_pos();
        return;
    }
    if (g_boss_state == BS_DYING) {
        if (!(t & 5)) fx_spawn(FX(bx - 16 + (rnd8() & 31)), FX(by - 16 + (rnd8() & 31)));
        if (t >= 120) g_boss_state = BS_DONE;
        return;
    }
    if (!broken) {
        plumb_pos();
        if (g_boss_hp <= PLUMB_HALF) {                        /* 鎖が切れる: 跳ね回る */
            broken = 1; fx = FX(bx); fy = FX(by); fvx = bx < 128 ? 24 : -24; fvy = 0;
            fx_spawn(FX(128), FX(anchor_y + 40));
        }
        if (++aim_t >= 45) { aim_t = 0; enemy_shoot(FX(bx), FX(by + 14)); }
        if ((t & 127) == 64) { enemy_bullet(FX(bx), FX(by + 14), 0, 40); enemy_bullet(FX(bx), FX(by + 14), -12, 38); enemy_bullet(FX(bx), FX(by + 14), 12, 38); }
    } else {
        fx += fvx; fy += fvy; fvy += 2;
        if (fy > FX(GROUND - 14)) {                           /* 跳ねる: 3 方向 */
            fy = FX(GROUND - 14); fvy = -56;
            enemy_bullet(fx, fy, -32, -8); enemy_bullet(fx, fy, -28, -20); enemy_shoot(fx, fy);
        }
        if (fx < FX(24) || fx > FX(232)) fvx = (s16)-fvx;
        bx = PX(fx); by = PX(fy);
    }
    {
        u8 n = shot_hit(bx, by + 2, 14);
        u8 w = shot_hit(bx, by - 12, 5);                      /* 吊り輪: 2 倍 */
        while (n--) boss_damage(1);
        while (w--) boss_damage(2);
        touch(bx, by + 2, 12);
    }
}

void boss_update(void) {
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE) return;
    t++;
    shots_prepare();
    if (kind == BOSS_LEGION) legion_update(); else plumb_update();
}

void boss_draw(void) {
    u8 i;
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE) return;
    if (kind == BOSS_LEGION) {
        for (i = 0; i < NPAWN; i++) {
            s16 x, y;
            if (g_boss_state == BS_DYING) { x = PX(pawn_x[i]); y = PX(pawn_y[i]); }
            else { x = bx + pawn_dx[i]; y = pawn_off[i] ? PX(pawn_y[i]) : by + pawn_dy[i]; }
            spr_put(x - 8, y - 8, PAT_PAWN, G_ENEMY, 0);
        }
        if (g_boss_state != BS_DYING) spr_put(bx - 8, by - 20, PAT_LEGION_CORE, G_ENEMY, 0);
        return;
    }
    if (g_boss_state == BS_DYING && (t & 2)) return;          /* 点滅しながら爆発 */
    spr_put(bx - 16, by - 16, PAT_BOB_TL, G_ENEMY, 0);
    spr_put(bx,      by - 16, PAT_BOB_TR, G_ENEMY, 0);
    spr_put(bx - 16, by,      PAT_BOB_BL, G_ENEMY, 0);
    spr_put(bx,      by,      PAT_BOB_BR, G_ENEMY, 0);
    if (!broken) {                                            /* 吊り元から吊り輪まで 7 等分(1/7 ≒ 37/256) */
        s16 stx = (s16)((bx - 128) * 37), sty = (s16)((by - 16 - anchor_y) * 37), ax = stx, ay = sty;
        for (i = 0; i < CHAIN_N; i++, ax += stx, ay += sty)
            spr_put((s16)(128 + (ax >> 8)) - 8, (s16)(anchor_y + (ay >> 8)) - 8, PAT_CHAIN, G_ENEMY, 0);
    }
}
