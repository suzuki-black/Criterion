/* s3boss.c — 3 面の中ボス METRONOME とボス TACHOMETER(指示書 5.3)。
   ・METRONOME: 三角錐の本体(スプライト 2 x 3)と、上へ伸びる振り子の竿と錘。振り子が右に振れるとスクロールは
     前進、左で逆走、真ん中で一瞬止まる(振り子の角度 → g_scroll_speed)。周期はだんだん速くなる。錘を壊すと
     テンポが崩れて止まり、先へ進む。★BGM のテンポとの同期は M7。
   ・TACHOMETER: 画面の右半分の回転計。盤は BG(ページ 2 に読んでおいた 128x128 を表示リングへ写す)。針は
     ROTOR(スプライト用、32x32)で回し、先に節のスプライト。針がレッドゾーンに入ると、本体が自機の方へ突進する
     (スクロールを最高速で前へ → 戻る。盤は BG なので画面ごと迫ってくる)。目盛りの数字(スプライト 8 個)は
     1 つずつ壊せて、全部壊すと針が暴走して本体を壊す。 */
#include "s3.h"
#include "spr.h"
#define SPRITE_NEEDLE32
#include "sprite_data.h"
#include "vdp.h"
#include "rom.h"
#include "rotor.h"
#include "ghost.h"
#include "assets.h"
#include "s3_data.h"
#include "sys.h"

extern u16 g_frame_t0;             /* main.c: このフレームの始まり */

static u8  kind;
static u16 t;

/* ---- 自機のショットとの当たり ---- */
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
static u8 shot_hit(s16 x, s16 y, u8 r) {
    u8 k, n = 0, ux, uy;
    if (x < 0 || x > 255 || y < 0 || y > 255) return 0;
    ux = (u8)(x - r); uy = (u8)(y - r);
    for (k = 0; k < nsh; k++) {
        Bullet *b;
        if ((u8)(shx[k] - ux) >= (u8)(r * 2) || (u8)(shy[k] - uy) >= (u8)(r * 2)) continue;
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

/* ---- METRONOME ---- */
#define MET_X  200
#define MET_PY 150                 /* 振り子の支点(本体の下の方) */
#define ROD    56
static u16 ph;                     /* 振り子の位相(256 で 1/64 周) */
static u8  omega;                  /* 位相の進み(だんだん速く) */
static s8  ang;                    /* 振れ角(64 で一周) */
static s16 wx_, wy_;               /* 錘 */
static s8  last_side;

static void met_update(void) {
    if (g_boss_state == BS_ENTER) {
        if (g_scroll_speed > 0) g_scroll_speed--; else if (g_scroll_speed < 0) g_scroll_speed++;
        if (!g_scroll_speed && t > 30) g_boss_state = BS_FIGHT;
        return;
    }
    if (g_boss_state == BS_DYING) {                           /* テンポが崩れて止まる */
        if (g_scroll_speed > 0) g_scroll_speed--; else if (g_scroll_speed < 0) g_scroll_speed++;
        if (!(t & 3)) fx_spawn(FX(MET_X - 16 + (rnd8() & 31)), FX(MET_PY - 40 + (rnd8() & 47)));
        if (t >= 90) g_boss_state = BS_DONE;
        return;
    }
    if (!(t & 63) && omega < 80) omega += 4;                  /* テンポが上がる */
    ph += omega;
    ang = (s8)(g_sin64[(u8)(ph >> 8) & 63] / 22);            /* ±5(≒ ±30 度) */
    /* 右に振れると前進、左で逆走(最大 6 ドット/フレーム)。真ん中で止まる */
    g_scroll_speed = (s8)((s16)g_sin64[(u8)(ph >> 8) & 63] * 24 / 127);
    wx_ = (s16)(MET_X + ((g_sin64[(u8)ang & 63] * ROD) >> 7));
    wy_ = (s16)(MET_PY - ((g_sin64[(u8)(ang + 16) & 63] * ROD) >> 7));
    g_boss_aim = wy_;
    {   /* 錘(弱点) */
        u8 n = shot_hit(wx_, wy_, 9);
        while (n--) { if (g_boss_hp > 1) g_boss_hp--; else { g_boss_hp = 0; g_boss_state = BS_DYING; t = 0; score_add(400); return; } }
    }
    shot_hit(MET_X, MET_PY - 20, 16);                         /* 本体は弾を止める */
    touch(MET_X, MET_PY - 20, 14); touch(wx_, wy_, 7);
    {   /* 振り切ったところで撃つ */
        s8 side = ang > 3 ? 1 : ang < -3 ? -1 : 0;
        if (side && side != last_side) { enemy_shoot(FX(wx_), FX(wy_)); enemy_bullet(FX(wx_), FX(wy_), -32, side * 8); }
        last_side = side;
    }
}

static void met_draw(void) {
    u8 i;
    if (g_boss_state == BS_DYING && (t & 2)) return;
    for (i = 0; i < 6; i++)
        spr_put((s16)(MET_X - 16 + (i & 1) * 16), (s16)(MET_PY - 44 + (i >> 1) * 16), (u8)(PAT_MET0 + i), G_ENEMY, 0);
    if (g_boss_state == BS_FIGHT) {
        for (i = 1; i <= 3; i++)                              /* 竿の節 */
            spr_put((s16)(MET_X + (((wx_ - MET_X) * i) >> 2)) - 8, (s16)(MET_PY + (((wy_ - MET_PY) * i) >> 2)) - 8,
                    PAT_CHAIN, G_ENEMY, 0);
        spr_put(wx_ - 8, wy_ - 8, PAT_MW, G_ENEMY, 0);
    }
}

/* ---- TACHOMETER ---- */
#define DIAL_SX  128               /* 盤の左上(画面、突進していないとき) */
#define DIAL_Y   30
#define NMARK    8
static u16 dial_wx;                /* 盤の中心の世界の x */
static u8  dial_drawn, dial_load;
static u8  dial_off, dial_clean;
static u8  needle;                 /* 針の振れ(0 = 左下 .. 48 = 右下。64 で一周の単位) */
static u8  mark_hp[NMARK];
static u8  rush_t, wild_t;
static u8  nset, nready, nrow, n_drawn, pat_buf[128];
static const s8 mark_a[NMARK] = { 0, 6, 12, 18, 24, 30, 36, 42 };   /* 目盛りの位置(振れの単位) */

void b3_load_dial(void) { dial_load = 1; dial_off = 0; }

/* 振れ s(0..48)→ 画面の角度(64 で一周、x 右・y 下): 225 度(左下)から時計回り */
static u8 screen_ang(u8 s) { return (u8)(40 - s); }        /* 数学の角度 225° = 40 単位、時計回り = 減る */
static s16 dial_cx(void) { return (s16)(dial_wx - g_wx); }

static void tacho_update(void) {
    u8 k, a;
    s16 cx = dial_cx(), cy = DIAL_Y + 64;
    if (g_boss_state == BS_ENTER) { if (dial_drawn >= 4) g_boss_state = BS_FIGHT; return; }
    if (g_boss_state == BS_DYING) {
        needle = (u8)(needle + 5);                            /* 針の暴走 */
        if (!(t & 3)) fx_spawn(FX(cx - 48 + (rnd8() & 95)), FX(cy - 48 + (rnd8() & 95)));
        g_scroll_speed = 0;
        if (t >= 120) g_boss_state = BS_DONE;
        return;
    }
    /* 針: ゆっくり右へ振れ、レッドゾーン(40..48)で突進 */
    if (rush_t) {
        rush_t++;
        /* 迫って、戻る(盤ごと)。速さは 1 フレーム 4(= 1 ドット)ずつ変える(いきなり 8 ドットにすると、
           見えてくる列の描き込みが追いつかずに遅れた) */
        /* 戻りは時間で決めず、元の位置(盤が DIAL_SX に見える g_wx)からのずれ d で止める。時間で決めると突進の
           たびに後ろへずれて盤が右へ流れ、右端の目盛りが画面から出た。窓から外れた盤の列は地図で描き直される */
        s16 d = (s16)(g_wx - (u16)(dial_wx - DIAL_SX - 64));
        s8 want = 0;
        if (rush_t < 20) want = g_calib ? -32 : 32;           /* 再校正版: 逆へ(下がってから迫る) */
        else {
            s16 v = -g_scroll_speed;                          /* 戻る速さ(1/4 ドット) */
            if (v < 0) v = 0;
            if (g_calib) {                                    /* 逆: 元の位置より後ろなら前へ */
                s16 u = g_scroll_speed > 0 ? g_scroll_speed : 0;
                if (-d > (s16)(((u * u) >> 5) + (u >> 3) + 1)) want = 32;
                else if (!g_scroll_speed && d < 0) want = 4;
                else if (!g_scroll_speed && d > 0) want = -4;
            } else if (d > (s16)(((v * v) >> 5) + (v >> 3) + 1)) want = -32;   /* 止まるまでに進む分(4 ずつ減らす)より遠い */
            else if (!g_scroll_speed && d > 0) want = -4;     /* 端数は 1 ドットずつ */
            else if (!g_scroll_speed && d < 0) want = 4;
        }
        if (g_scroll_speed < want) g_scroll_speed += 4; else if (g_scroll_speed > want) g_scroll_speed -= 4;
        if (rush_t >= 20 && !g_scroll_speed && !want && d == 0) { rush_t = 0; needle = 0; }
        if (rush_t == 255) rush_t = 254;                      /* 戻り切るまで(桁あふれで突進をやり直さない) */
    } else if (!(t & 7)) {
        needle++;
        if (needle >= 44) rush_t = 1;
    }
    /* 目盛りの数字 */
    g_boss_aim = -1;
    for (k = 0; k < NMARK; k++) {
        s16 mx, my;
        if (!mark_hp[k]) continue;
        a = screen_ang((u8)mark_a[k]);
        mx = (s16)(cx + ((g_sin64[(u8)(a + 16) & 63] * 50) >> 7)); my = (s16)(cy - ((g_sin64[a & 63] * 50) >> 7));
        if (g_boss_aim < 0 && mx > PX(g_pl.x) + 24) g_boss_aim = my;
        if (shot_hit(mx, my, 7) && --mark_hp[k] == 0) { fx_spawn(FX(mx), FX(my)); score_add(50); }
    }
    g_boss_hp = 0;
    for (k = 0; k < NMARK; k++) g_boss_hp += mark_hp[k];
    if (!g_boss_hp) { g_boss_state = BS_DYING; t = 0; score_add(1000); return; }
    /* ★盤も軸も弾を止めない(止めると右半分の目盛り、軸のすぐ下の目盛りに届かなかった) */
    touch(cx, cy, 48);
    if ((t & 63) == 0) { enemy_shoot(FX(cx), FX(cy)); }
}

static void tacho_draw(void) {
    u8 k, a;
    s16 cx = dial_cx(), cy = DIAL_Y + 64;
    if (g_boss_state == BS_NONE || dial_drawn < 4) return;
    for (k = 0; k < NMARK; k++) {
        if (!mark_hp[k]) continue;
        a = screen_ang((u8)mark_a[k]);
        spr_put((s16)(cx + ((g_sin64[(u8)(a + 16) & 63] * 50) >> 7)) - 8, (s16)(cy - ((g_sin64[a & 63] * 50) >> 7)) - 8,
                PAT_DIG, G_ENEMY, 0);
    }
    if (nset != 0xFF) {                                       /* 針(ROTOR)と先の節 */
        u8 p = (u8)(44 + nset * 4);
        spr_put(cx - 16, cy - 16, p, G_MISC, CS_CURSOR);     spr_put(cx, cy - 16, (u8)(p + 1), G_MISC, CS_CURSOR);
        spr_put(cx - 16, cy, (u8)(p + 2), G_MISC, CS_CURSOR); spr_put(cx, cy, (u8)(p + 3), G_MISC, CS_CURSOR);
        a = screen_ang(needle);
        spr_put((s16)(cx + ((g_sin64[(u8)(a + 16) & 63] * 30) >> 7)) - 8, (s16)(cy - ((g_sin64[a & 63] * 30) >> 7)) - 8,
                PAT_SAT, G_ENEMY, 0);
    }
}

/* 行優先の 32x32 → 16x16 のスプライト 4 枚 */
static void to_patterns(void) {
    u8 q, h, r, *d = pat_buf;
    for (q = 0; q < 4; q++)
        for (h = 0; h < 2; h++)
            for (r = 0; r < 16; r++) *d++ = ROT1_DST2[(u16)(((q >> 1) * 16u + r) * 4u + (q & 1) * 2u + h)];
}

/* ---- 入口 ---- */
void b3_start(u8 k) {
    u8 i;
    kind = k; t = 0; g_boss_state = BS_ENTER; g_boss_dmg = 0;
    if (k == B3_MET) { g_boss_hp = 40; ph = 0; omega = 16; last_side = 0; }
    else {
        dial_wx = (u16)(g_wx + DIAL_SX + 64); dial_drawn = 0; dial_clean = 0; needle = 0; rush_t = 0; wild_t = 0;
        for (i = 0; i < NMARK; i++) mark_hp[i] = 3;
        g_boss_hp = NMARK * 3;
        rotor_1bpp_src_at(spr_needle32, ROT1_SRC2);
        nset = 0xFF; nready = 0; nrow = 0; n_drawn = 0xFF;
    }
}

void b3_update(void) {
    if (g_boss_dmg) {                                         /* LIMIT BREAK: 錘 / 目盛りに */
        u8 d = g_boss_dmg; g_boss_dmg = 0;
        if (g_boss_state == BS_FIGHT) {
            if (kind == B3_TACHO) {                           /* 目盛りへ順に(体力は目盛りの合計から数え直すので、そちらを減らす) */
                u8 k;
                for (k = 0; k < NMARK && d; k++)
                    while (mark_hp[k] > 1 && d) { mark_hp[k]--; d--; }   /* 最後の 1 は弾で壊す */
            } else if (g_boss_hp > d) g_boss_hp -= d; else g_boss_hp = 1;
        }
    }
    g_boss_aim = -1;
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE) return;
    t++;
    shots_prepare();
    if (kind == B3_MET) met_update(); else tacho_update();
}

void b3_draw(void) {
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE) return;
    if (kind == B3_MET) met_draw(); else tacho_draw();
}

void b3_vram(void) {
    if (dial_load && (u16)(sys_timer() - g_frame_t0) < 150u * 16u) {
        /* 盤をページ 2 の 0..127 行・0..127 ドットへ(1 フレーム 4 行 = 256 バイト。8 行では最高速の区間で遅れた。
           重いフレームは次へ) */
        u8 r;
        for (r = 0; r < 4; r++, dial_off++)
            rom_to_vram(ASSET_TACHO_BANK, (u16)(ASSET_TACHO_OFF + dial_off * 64u), 1, (u16)(dial_off * 128u), 64);
        if (dial_off >= 128) dial_load = 0;
    }
    if (kind == B3_TACHO && dial_drawn && (g_boss_state == BS_DONE || g_boss_state == BS_NONE)) {
        /* 倒した: 盤の 16 列を描き直してチューブへ戻す(描き直しの頼みは 8 個まで。いっぱいなら次のフレーム。★s3.c は
           BS_DONE を同じフレームで BS_NONE にするので、BS_DONE だけを見ると盤が残った) */
        while (dial_clean < 16 && stage_redraw_col((u16)(((dial_wx - 64) >> 3) + dial_clean))) dial_clean++;
        if (dial_clean >= 16) { dial_drawn = 0; dial_clean = 0; }
    }
    if (kind == B3_TACHO && g_boss_state != BS_NONE) {
        if (dial_drawn < 4 && g_boss_state == BS_ENTER && !g_scroll_speed && !stage_busy() && gq_free() >= 8) {
            /* 盤を表示リングへ(32 行ずつ 4 回。128x128 を 1 回で写すと VDP が 50ms 塞がり、CPU の VRAM の書き込みも遅れた)。
               ★列の流し込みが追いついてから(最高速の区間で遅れていた列が、写した盤の上に描かれて消えた) */
            ring_cmd(0, (u16)(512u + dial_drawn * 32u), DIAL_SX, (s16)(DIAL_Y + dial_drawn * 32), 128, 32, 0, 0);
            dial_drawn++;
        }
        /* 針: 8 行ずつ ROTOR、出来たらパターン 44..47 / 48..51 へ */
        if (nready) {
            vdp_write_addr17(1, (u16)(0xF800u + (u16)(44 + (nset == 0 ? 1 : 0) * 4) * 32u));
            vdp_out_block(128, pat_buf);
            nset = (u8)(nset == 0 ? 1 : 0); nready = 0;
        }
        /* 針の絵は振れが変わったときだけ作り直す(1 枚 32 行を 8 行ずつ。毎フレーム作ると突進の重いフレームと重なった) */
        if ((nrow || needle != n_drawn) && (u16)(sys_timer() - g_frame_t0) < 150u * 16u) {   /* 重いフレームは次へ */
            if (!nrow) n_drawn = needle;
            rot1_hi = 0xE8;
            rotor_1bpp32(ROT1_DST2, screen_ang(n_drawn), 256, nrow, 8);
            rot1_hi = 0x38;
            nrow += 8;
            if (nrow >= 32) { nrow = 0; to_patterns(); nready = 1; }
        }
    }
    gq_pump(0);
}
