/* boss.c — 1 面の中ボス LEGION と ボス PLUMB(指示書 5.1)。
   ・LEGION(仮。M3 の後段で BG の Pawn の群体にする): 9 体の Pawn が人型に積み上がり、胸に核(隊長機)。
     撃たれた Pawn は剥がれて地面に落ち、しばらくすると這い上がって穴を埋める。核を壊すと全体が崩れる。
   ・PLUMB: 上空から鎖(スプライト 12 節)で吊られた直径約 44 ドット(64x64 の枠)の真鍮の下げ振り。BG に描く。
     振り子で揺れる傾きは ROTOR(rotor.c)で前もって作った 7 枚(±34 度)から選ぶ。自転は胴の経線の縞
     (色 2..5)のパレットを回して見せる。体力が半分を切ると鎖が切れ、縮小した 3 枚(遠近)を使って跳ね回り、
     自転が速くなる。上部の吊り輪は 2 倍の傷。
     ★絵は都市後半に入った時点から ROTOR で 1 枚ずつ作り、ページ 2 に置く(boss_prepare / boss_vram_gen。
       1 フレームに 4 行の計算か 8 行の転送。10 枚で約 4 秒)。戦う間は 2 フレームに 1 回、ページ 2 の枠を表示リングへ
       HMMM で写し、前の位置からはみ出した分を空の色で塗る(GHOST の待ち行列)。広場の背景は空(色 1)なので
       枠の地も色 1 にして重ね描きする。雲の底(画面の 14 行より上)と歩道(158 行から)には描かない。
   座標はドット(中心)。画面の外の上は y < 0。 */
#include "game.h"
#include "spr.h"
#include "sprite_data.h"
#include "vdp.h"
#include "rom.h"
#include "rotor.h"
#include "ghost.h"
#include "assets.h"

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

/* ---- PLUMB ---- */
#define CHAIN_N 12
#define CHAIN_L 96                 /* 吊り元から下げ振りの中心まで */
#define RING_D  25                 /* 中心から吊り輪まで */
#define NFRAME  10
#define FR_UP   3                  /* 直立・等倍 */
#define FR_FAR  7                  /* 縮小 3 枚の先頭 */
#define SKY_TOP 14                 /* 雲の底の下(これより上には描かない) */
#define SKY_BOT 158                /* 歩道(ここから下には描かない) */
#define FLOOR2  126                /* 鎖が切れてから跳ねる床(枠の下端 = SKY_BOT) */
/* 枠の並び(gen_boss.py の FRAMES と同じ): 傾き -6..6(64 で一周)× 等倍、直立の縮小 0.85 / 0.7 / 0.55 */
static const s8  fr_angle[NFRAME] = { -6, -4, -2, 0, 2, 4, 6, 0, 0, 0 };
static const u16 fr_step[NFRAME]  = { 256, 256, 256, 256, 256, 256, 256, 301, 366, 465 };
static const u8  fr_r[NFRAME]     = { 14, 14, 14, 14, 14, 14, 14, 12, 10, 8 };   /* 当たりの半径 */
static const u8  brass[4][3] = { { 3, 2, 1 }, { 5, 4, 1 }, { 7, 6, 3 }, { 5, 4, 1 } };
static s16 anchor_y;
static s8  swing;
static u8  broken, cur_fr, spin;
static s16 fx, fy, fvx, fvy;       /* 鎖が切れてから(固定小数点) */
static u8  gen_k, gen_ph;          /* 絵を作る進み(gen_k == NFRAME で終わり) */
/* 表示リングに描いてある枠(画面の座標、切り詰めた後)。w = 0 で無し */
static s16 dr_x, dr_y;
static u8  dr_w, dr_h;
/* 場面の始め(game_start): 何も作っていない状態へ */
void boss_reset(void) {
    g_boss_state = BS_NONE; kind = 0xFF; gen_k = NFRAME; dr_w = 0; broken = 0;
}

void boss_start(u8 k) {
    u8 i;
    kind = k; t = 0; g_boss_state = BS_ENTER;
    if (k == BOSS_LEGION) {
        g_boss_hp = LEGION_HP; bx = 300; by = 118; spread_t = 0;
        for (i = 0; i < NPAWN; i++) { pawn_off[i] = 0; pawn_hp[i] = 2; }
    } else {
        g_boss_hp = PLUMB_HP; bx = 128; anchor_y = -110; broken = 0; aim_t = 0; spin = 0; dr_w = 0;
        cur_fr = FR_UP; swing = 0;
        while (gen_k < NFRAME) boss_vram_gen();               /* 間に合っていなければここで作り切る */
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
    s16 sx, cx;
    swing = (s8)(g_sin64[(u8)(t >> 1) & 63] >> 4);           /* 振れ角 ±7(64 で一周)≒ ±40 度 */
    sx = g_sin64[(u8)swing & 63]; cx = g_sin64[(u8)(swing + 16) & 63];
    bx = (s16)(128 + ((sx * CHAIN_L) >> 7));
    by = (s16)(anchor_y + ((cx * CHAIN_L) >> 7));
    cur_fr = (u8)(FR_UP - ((swing + (swing < 0 ? -1 : 1)) >> 1));   /* 右へ振れたら吊り輪は左上(角度は逆) */
    if (cur_fr > 6) cur_fr = swing > 0 ? 0 : 6;
}

/* 吊り輪の位置(弱点) */
static s16 ring_x(void) { return broken ? bx : (s16)(bx - ((g_sin64[(u8)swing & 63] * RING_D) >> 7)); }
static s16 ring_y(void) {
    if (broken) return (s16)(by - (s16)((RING_D * 256u / fr_step[cur_fr])));
    return (s16)(by - ((g_sin64[(u8)(swing + 16) & 63] * RING_D) >> 7));
}

static void plumb_update(void) {
    if (g_boss_state == BS_ENTER) {
        anchor_y += 2;
        if (anchor_y >= -8) { anchor_y = -8; g_boss_state = BS_FIGHT; }
        plumb_pos();
        return;
    }
    if (g_boss_state == BS_DYING) {
        if (!(t & 3)) fx_spawn(FX(bx - 24 + (rnd8() & 47)), FX(by - 24 + (rnd8() & 47)));
        if (t >= 120) g_boss_state = BS_DONE;
        return;
    }
    if (!broken) {
        plumb_pos();
        if (g_boss_hp <= PLUMB_HALF) {                        /* 鎖が切れる: 跳ね回る */
            broken = 1; fx = FX(bx); fy = FX(by); fvx = bx < 128 ? 24 : -24; fvy = 0;
            fx_spawn(FX(128), FX(anchor_y + 40));
        }
        if (++aim_t >= 45) { aim_t = 0; enemy_shoot(FX(bx), FX(by + 20)); }
        if ((t & 127) == 64) {                                /* 測定ビーム: 先端から下へ */
            enemy_bullet(FX(bx), FX(by + 24), 0, 40); enemy_bullet(FX(bx), FX(by + 24), -12, 38);
            enemy_bullet(FX(bx), FX(by + 24), 12, 38);
        }
    } else {
        s8 d = g_sin64[(u8)(t >> 1) & 63];                    /* 遠近: 近い(等倍)〜遠い(0.55) */
        fx += fvx; fy += fvy; fvy += 2;
        if (fy > FX(FLOOR2)) {                                /* 跳ねる: 3 方向 */
            fy = FX(FLOOR2); fvy = -56;
            enemy_bullet(fx, fy, -32, -8); enemy_bullet(fx, fy, -28, -20); enemy_shoot(fx, fy);
        }
        if (fx < FX(40) || fx > FX(216)) fvx = (s16)-fvx;
        bx = PX(fx); by = PX(fy);
        cur_fr = d < 32 ? FR_UP : (u8)(FR_FAR + ((u8)(d - 32) >> 5));
    }
    {
        u8 n = shot_hit(bx, by + 2, fr_r[cur_fr]);
        u8 w = shot_hit(ring_x(), ring_y(), 5);               /* 吊り輪: 2 倍 */
        while (n--) boss_damage(1);
        while (w--) boss_damage(2);
        touch(bx, by + 2, (u8)(fr_r[cur_fr] - 2));
    }
    if (!(t & (broken ? 0 : 3))) {                            /* 自転: 縞のパレットを回す(切れたら速く) */
        u8 i;
        spin++;
        for (i = 0; i < 4; i++) {
            const u8 *c = brass[(u8)(i + spin) & 3];
            vdp_set_pal((u8)(2 + i), c[0], c[1], c[2]);
        }
    }
}

/* ---- PLUMB の絵: 作る(ROTOR → ページ 2)と、表示リングへ写す ---- */
#define SLOT_X(k) ((u16)(((k) & 3) * 64u))
#define SLOT_Y(k) ((u16)(512u + ((k) >> 2) * 64u))

static u16 src_off;                /* 元の絵を ROM から読んだバイト数(1 段 512 バイト) */

void boss_prepare(u8 k) {
    if (k != BOSS_PLUMB) return;
    gen_k = 0; gen_ph = 0; src_off = 0;
}

/* 1 段: 0..15 = 4 行ずつ計算(約 20 ライン)、16..23 = 8 行ずつ VRAM へ(256 バイト、約 36 ライン)。
   ★8 行ずつ計算すると、敵の多いフレームと重なって遅れた */
void boss_vram_gen(void) {
    if (gen_k >= NFRAME) return;
    if (src_off < ASSET_BOB_LEN) {                            /* まず元の絵(2KB)を 4 段で */
        rom_to_ram(ASSET_BOB_BANK, (u16)(ASSET_BOB_OFF + src_off), ROTOR_SRC + src_off, 512);
        src_off += 512;
        return;
    }
    if (gen_ph < 16) {
        if (gen_ph == 0) rotor_begin(64, (u8)fr_angle[gen_k], fr_step[gen_k], 1);
        rotor_rows(ROTOR_DST + gen_ph * 128u, 4);
    } else {
        u8 r, r0 = (u8)((gen_ph - 16) * 8);
        for (r = r0; r < r0 + 8; r++) {
            vdp_write_addr17(1, (u16)(((gen_k >> 2) * 64u + r) * 128u + (gen_k & 3) * 32u));
            vdp_out_block(32, ROTOR_DST + r * 32u);
        }
    }
    if (++gen_ph >= 24) { gen_ph = 0; gen_k++; }
}

/* 画面の矩形 → 表示リング(ページ 0|1)。リングの 512 幅と 256 行の継ぎ目で分ける */
static void ring_cmd(u16 sx, u16 sy, s16 x, s16 y, u8 w, u8 h, u8 fill) {
    u16 rx = (u16)((g_wx + (u16)x) & 511u);
    u8 ry = (u8)(y + g_vscroll);
    u8 w1 = w, h1 = h;
    if (rx < 256 && rx + w > 256) w1 = (u8)(256 - rx);
    else if (rx >= 256 && rx + w > 512) w1 = (u8)(512 - rx);
    if ((u16)ry + h > 256) h1 = (u8)(256 - ry);
    {
        u16 dx = rx & 255, dy = (u16)(rx >= 256 ? 256u + ry : ry);
        if (fill) gq_hmmv(dx, dy, w1, h1, 0x11); else gq_hmmm(sx, sy, dx, dy, w1, h1);
    }
    if (w1 < w) ring_cmd((u16)(sx + w1), sy, (s16)(x + w1), y, (u8)(w - w1), h1, fill);
    if (h1 < h) ring_cmd(sx, (u16)(sy + h1), x, (s16)(y + h1), w, (u8)(h - h1), fill);
}

static void erase(s16 x, s16 y, s16 w, s16 h) {
    if (w > 0 && h > 0) ring_cmd(0, 0, x, y, (u8)w, (u8)h, 1);
}

static void plumb_blit(u8 show) {
    s16 x = (s16)((bx - 32) & ~1), y = (s16)(by - 32), w = 64, h = 64, ox = 0, oy = 0;
    if (!show) w = h = 0;
    if (y < SKY_TOP) { oy = SKY_TOP - y; h -= oy; y = SKY_TOP; }
    if (y + h > SKY_BOT) h = SKY_BOT - y;
    if (x < 0) { ox = -x; w -= ox; x = 0; }
    if (x + w > 256) w = 256 - x;
    if (w <= 0 || h <= 0) w = h = 0;
    /* 前の枠のうち、新しい枠に入らない部分を空の色に(左右の帯と上下の帯) */
    if (dr_w) {
        s16 ox0 = dr_x, oy0 = dr_y, ox1 = dr_x + dr_w, oy1 = dr_y + dr_h;
        if (!w || x >= ox1 || x + w <= ox0 || y >= oy1 || y + h <= oy0) erase(ox0, oy0, dr_w, dr_h);
        else {
            s16 ix0 = x > ox0 ? x : ox0, ix1 = x + w < ox1 ? x + w : ox1;
            erase(ox0, oy0, ix0 - ox0, dr_h);                 /* 左 */
            erase(ix1, oy0, ox1 - ix1, dr_h);                 /* 右 */
            erase(ix0, oy0, ix1 - ix0, y - oy0);              /* 上 */
            erase(ix0, y + h, ix1 - ix0, oy1 - (y + h));      /* 下 */
        }
    }
    if (w) ring_cmd((u16)(SLOT_X(cur_fr) + ox), (u16)(SLOT_Y(cur_fr) + oy), x, y, (u8)w, (u8)h, 0);
    dr_x = x; dr_y = y; dr_w = (u8)w; dr_h = (u8)h;
}

/* VRAM の窓で毎フレーム(stage.c): 絵を作る段と、PLUMB を写す */
void boss_vram(void) {
    if (gen_k < NFRAME) boss_vram_gen();
    if (kind != BOSS_PLUMB) return;
    /* ★1 回の描き替えは最大 20 コマンド(写し 4 + 消し 4 方向 × リングの継ぎ目)。入りきらないときは見送る
       (途中まで積むと、消し残しや描き残しが出る) */
    if (gq_free() >= 20) {
        if (g_boss_state == BS_DONE || g_boss_state == BS_NONE) {
            if (dr_w) plumb_blit(0);                          /* 最後に消す */
        } else if (!(t & 1)) plumb_blit(1);                   /* 30Hz */
    }
    gq_pump(250);
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
    if (!broken && g_boss_state != BS_DYING) {               /* 吊り元から吊り輪まで 13 等分(1/13 ≒ 20/256) */
        s16 rx = ring_x(), ry = ring_y();
        s16 stx = (s16)((rx - 128) * 20), sty = (s16)((ry - anchor_y) * 20), ax = stx, ay = sty;
        for (i = 0; i < CHAIN_N; i++, ax += stx, ay += sty)
            spr_put((s16)(128 + (ax >> 8)) - 8, (s16)(anchor_y + (ay >> 8)) - 8, PAT_CHAIN, G_ENEMY, 0);
    }
}
