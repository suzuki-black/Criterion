/* s3.c — 3 面「臨界加速」のオーバレイ: 区間・速さ・停止・逆走・隔壁(指示書 5.3)。
   月面の上の星空 → 月面へ降りてチューブの入口 → 加速(0.5 → 1 → 2 → 4 → 8 ドット/フレーム)→ 急停止
   (Scanner の測定線が縦断)→ 逆走(窓を左へ。編隊は左から)→ 中ボス METRONOME(振り子がスクロールを
   前後させる)→ 最高速 → ボス TACHOMETER → 射出口から宇宙へ。
   ・速さは g_scroll_speed(1/4 ドット単位、負 = 逆走)。コストは見えてくる列の描き込みだけ(stage.c)。
   ・高速(4 ドット/フレーム以上)では照明パネルの短冊を流線版に置き換える(g_strip_xlat)。
   ・隔壁は 1.5 秒ごとにすき間の位置が変わる(地図を書き換えて列を描き直す = 列単位の BG の書き換え)。
   ・壁と隔壁は固い(自機の上下の限り)。
   ★読み込まれるのは 3 面の空中戦が始まってから(2 面のオーバレイが頼む)。 */
#include "s3.h"
#include "ovl.h"
#include "vdp.h"
#include "rom.h"
#include "assets.h"
#include "s3_data.h"
#include "spr.h"
#include "sprite_data.h"

#define AIR_LEN  384
#define SINK_LEN 256
#define S_TOP    (-192)

static u16 sec_wx, sec_t;
static u8  bulk_t;
static s16 scan_x;                 /* Scanner の測定線(停止の間) */
static const u8 inside[3] = { 1, 1, 2 };

static void set_streak(u8 on) {
    u8 i;
    for (i = 6; i < 10; i++) g_strip_xlat[i] = (u8)(on ? i + 4 : i);
    g_strip_xlat[14] = (u8)(on ? 15 : 14);
}

static void enter(u8 sec) {
    g_sec = sec; sec_wx = g_wx; sec_t = 0;
    g_wave_mirror = 0;
    switch (sec) {
    case SEC3_AIR: case SEC3_SINK: case SEC3_ACCEL: case SEC3_TOP: enemy_waves(WAVES_AIR); break;
    case SEC3_REV: g_wave_mirror = 1; enemy_waves(WAVES_AIR); break;
    default: enemy_waves(WAVES_NONE); break;
    }
    if (sec == SEC3_REV) g_col_rev = 31;                      /* 逆走: 窓は画面の左へ */
    if (sec == SEC3_MID) { g_col_rev = 16; b3_start(B3_MET); }   /* 振り子で前後: 窓は左右に半分ずつ */
    if (sec == SEC3_TOP) {
        g_col_rev = 0; g_scroll_speed = g_calib ? 4 : 32; set_streak(!g_calib);
        g_map[S3_COL_STOP + 2] = 17; g_map[S3_COL_STOP + 6] = 16;   /* 停止の隔壁は開いている(まだ窓の外) */
        if (!g_calib) b3_load_dial();
    }
    if (sec == SEC3_BOSS) { g_col_rev = 16; b3_start(B3_TACHO); }  /* 突進で前後するので窓は左右に */
    if (sec == SEC3_OUT) { g_col_rev = 0; set_streak(0); }
}

void ovl_init(void) {
    stage_reset_ground();
    g_w0 = (u16)((g_wx + AIR_LEN) >> 3);
    rom_to_ram(ASSET_S3_MAP_BANK, ASSET_S3_MAP_OFF, g_map, S3_COLS);
    g_maplen = S3_COLS;
    stage_load_strips(ASSET_S3_STRIPS_BANK, ASSET_S3_STRIPS_OFF);
    vdp_set_pal(6, 2, 2, 3);                                  /* 壁の影(暗い灰) */
    g_boss_state = BS_NONE; g_grav = GRAV_NONE; bulk_t = 0;
    if (g_calib) {                                            /* 7 面の試験 03: 白い空間から最高速の区間の終わりへ沈んで TACHOMETER */
        g_w0 = (u16)((g_wx >> 3) - (S3_COL_ARENA - 8 - 64));
        b3_load_dial();
        calib_begin(3);
        enter(SEC3_AIR);                                      /* 短い通路(短冊を読む間)→ 沈む */
        return;
    }
    enter(SEC3_AIR);
}

static u16 col_of_screen(void) { return (u16)((g_wx >> 3) - g_w0); }

/* 自機の上下の限り(壁と隔壁) */
static void terrain(void) {
    u16 c0 = (u16)(((g_wx + (u16)PX(g_pl.x)) >> 3) - g_w0), c;
    s16 lo = 0, hi = PLAY_H - 16;
    if (g_sec < SEC3_SINK || g_sec == SEC3_OUT) { g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16; return; }
    for (c = c0; c <= (u16)(c0 + 2); c++) {
        u8 k;
        if (c >= S3_COLS) continue;
        k = g_map[c];
        if (s3_ymin[k] > lo) lo = s3_ymin[k];
        if (s3_ymax[k] < hi) hi = s3_ymax[k];
    }
    lo -= g_scam; hi -= g_scam;
    if (hi < lo) hi = lo;                                     /* 閉じた隔壁: 押し戻される(上下には動けない) */
    g_pl_ymin = lo < 0 ? 0 : lo;
    g_pl_ymax = hi > PLAY_H - 16 ? PLAY_H - 16 : hi;
    /* 閉じた隔壁(すき間なし)の列に入ったら、左へ押し戻す */
    if (s3_ymax[g_map[(u16)(c0 + 2) < S3_COLS ? (u16)(c0 + 2) : 0]] == 28 && g_pl.x > FX(8)) g_pl.x -= FX(1);
}

/* 隔壁: 窓の中のものを 1.5 秒ごとにすき間の位置を変えて描き直す */
static void bulkheads(void) {
    u8 i;
    if (++bulk_t < 90) return;
    bulk_t = 0;
    for (i = 0; i < S3_NBULK; i++) {
        u16 c = s3_bulk[i];
        s16 sx = (s16)((g_w0 + c) * 8u - g_wx);
        if (sx < 40 || sx > 400) continue;                    /* 自機の真上は替えない */
        g_map[c] = (u8)(g_map[c] == 18 ? 16 : g_map[c] + 1);
        stage_redraw_col((u16)(g_w0 + c));
    }
}

void ovl_update(void) {
    sec_t++;
    switch (g_sec) {
    case SEC3_AIR:
        if ((u16)(g_wx - sec_wx) >= (g_calib ? 128u : AIR_LEN)) enter(SEC3_SINK);
        break;
    case SEC3_SINK: {
        u16 d = (u16)(g_wx - sec_wx);
        if (d >= SINK_LEN) { g_scam_q = 0; enter(g_calib ? SEC3_TOP : SEC3_ACCEL); }
        else g_scam_q = (s16)(S_TOP * 16 + (s16)(d * 12u));
        break;
    }
    case SEC3_ACCEL: {
        u16 c = col_of_screen();
        s8 sp = c < 96 ? 2 : c < 144 ? 4 : c < 192 ? 8 : c < 240 ? 16 : 32;   /* 0.5 → 1 → 2 → 4 → 8 */
        if (g_scroll_speed < sp && !(sec_t & 3)) g_scroll_speed++;
        set_streak(g_scroll_speed >= 16);
        bulkheads();
        if (c + 34 >= S3_COL_STOP) enter(SEC3_STOP);
        break;
    }
    case SEC3_STOP:
        /* 1/2 秒で 0 に。静寂の中、Scanner の測定線が左から右へ縦断する */
        if (g_scroll_speed > 0) { g_scroll_speed -= 2; if (g_scroll_speed < 0) g_scroll_speed = 0; set_streak(0); }
        if (sec_t == 40) scan_x = 0;
        if (sec_t > 40) {
            scan_x += 3;
            if (g_pl.alive && PX(g_pl.x) + 8 > scan_x - 3 && PX(g_pl.x) + 8 < scan_x + 3) player_hit();
        }
        if (sec_t > 40 + 90) enter(SEC3_REV);
        break;
    case SEC3_REV:
        if (g_scroll_speed > -16 && !(sec_t & 3)) g_scroll_speed--;   /* 逆へ加速 */
        if (col_of_screen() <= S3_COL_ACCEL + 88) enter(SEC3_MID);
        break;
    case SEC3_MID:
    case SEC3_BOSS:
        if (sec_t == 3600) g_boss_state = BS_DONE;
        if (g_boss_state == BS_DONE) {
            g_boss_state = BS_NONE;
            enter(g_sec == SEC3_MID ? SEC3_TOP : SEC3_OUT);
        }
        break;
    case SEC3_TOP:
        bulkheads();
        if (col_of_screen() + 8 >= S3_COL_ARENA) { g_scroll_speed = 0; set_streak(0); enter(SEC3_BOSS); }
        break;
    case SEC3_OUT:
        if (g_calib) { if (calib_out(sec_t)) return; break; }  /* 試験に合格 */
        if (g_scroll_speed < 32 && !(sec_t & 3)) g_scroll_speed++;
        if (g_scam_q > S_TOP * 16) g_scam_q -= 24;             /* 射出口から宇宙へ */
        if (sec_t == 240) {                                   /* 4 面へ(このあと 3 面のコードは呼ばれない) */
            stage_banner("STAGE 4 / STRATA");
            ovl_request(OVL_S4);
            g_sec = SEC3_OUT + 1;                              /* = 4 面の空中戦(SEC4_AIR) */
        }
        break;
    default:
        break;
    }
    terrain();
}

void ovl_logic(void) { b3_update(); }

void ovl_draw(void) {
    b3_draw();
    if (g_sec == SEC3_STOP && sec_t > 40) {                   /* 測定線(スプライトを縦に 12 枚) */
        u8 i;
        for (i = 0; i < 12; i++) spr_put(scan_x - 8, (s16)(i * 16), PAT_SCAN, G_ENEMY, 0);
    }
    if (g_scroll_speed >= 16 && g_pl.alive) {                 /* 高速: 自機の残像 */
        spr_put(PX(g_pl.x) - 10, PX(g_pl.y), PAT_SHIP_A, G_MISC, CS_DIM);
        spr_put(PX(g_pl.x) - 22, PX(g_pl.y), PAT_SHIP_A, G_MISC, CS_DIM);
    }
}

void ovl_vram(void) { b3_vram(); }

void ovl_bands(s16 sc, u16 nx) {
    (void)nx;
    stage_ev_pal((s16)(0 - sc), inside);
}
