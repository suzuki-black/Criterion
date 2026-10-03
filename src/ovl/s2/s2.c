/* s2.c — 2 面「重力井戸」のオーバレイ: 区間・重力・地形の当たり・天地のロール(指示書 5.2)。
   空中戦(1 面の上昇から続く星空)→ 残骸の帯へ沈む → 重力・下 → 重力・反転(1/2 秒のロール)→ 中ボス
   COUNTERWEIGHT → 重力・中心 → ボス KILOGRAM → 重力が消え、星空へ上がって 3 面へ。
   ・重力は本体(player.c / enemy.c)が自機・敵弾・アイテムに効かせる。ここは向きを決めるだけ。
   ・地形: 短冊ごとの天井の行数と床の上端(s2_data.h)から、自機の上下の限りを毎フレーム決める(残骸は固い)。
   ・ロール: R#23 にだけ g_roll を足す(スプライトは動かさない)。リングの 256 行が一周する。
   ★読み込まれるのは 2 面の空中戦が始まってから(1 面のオーバレイが頼む)。地面(短冊)は 1 面のものを捨てて
     2 面のものを読む(地面が画面に出ていない空中戦の間に)。 */
#include "s2.h"
#include "ovl.h"
#include "vdp.h"
#include "rom.h"
#include "assets.h"
#include "s2_data.h"

#define AIR_LEN  384               /* 空中戦の残り(ドット) */
#define SINK_LEN 256               /* 沈む(g_scam が -192 → 0) */
#define S_TOP    (-192)

static u16 sec_wx, sec_t;
static const u8 space[3] = { 0, 0, 1 };

static void enter(u8 sec) {
    g_sec = sec; sec_wx = g_wx; sec_t = 0;
    switch (sec) {
    case SEC2_SINK: case SEC2_DOWN: case SEC2_FLIP: case SEC2_CENTER: enemy_waves(WAVES_AIR); break;
    default: enemy_waves(WAVES_NONE); break;
    }
    if (sec == SEC2_DOWN) { g_grav = GRAV_DIR; g_gx = 0; g_gy = 1; }
    if (sec == SEC2_MID) { g_grav = GRAV_NONE; b2_start(B2_CW); }
    if (sec == SEC2_CENTER) { g_grav = GRAV_CENTER; g_gcx = 128; g_gcy = 94; if (!g_calib) b2_prepare(); }
    if (sec == SEC2_BOSS) { g_grav = GRAV_NONE; b2_start(B2_KG); }
    if (sec == SEC2_OUT) {
        u8 i;
        g_grav = GRAV_NONE; g_scroll_speed = 4;
        for (i = 0; i < MAX_ESHOT; i++) { g_eshot[i].vx = 0; g_eshot[i].vy = 0; }   /* 残骸(弾)が一斉に静止する */
    }
}

void ovl_init(void) {
    stage_reset_ground();
    g_w0 = (u16)((g_wx + AIR_LEN) >> 3);
    rom_to_ram(ASSET_S2_MAP_BANK, ASSET_S2_MAP_OFF, g_map, S2_COLS);
    g_maplen = S2_COLS;
    stage_load_strips(ASSET_S2_STRIPS_BANK, ASSET_S2_STRIPS_OFF);
    g_boss_state = BS_NONE;
    if (g_calib) {                                            /* 7 面の試験 02: 白い空間から中心の区間の終わりへ沈んで KILOGRAM */
        g_w0 = (u16)((g_wx >> 3) - (S2_COL_ARENA2 - 8 - 48));
        b2_prepare();
        calib_begin(2);
        enter(SEC2_SINK);
        return;
    }
    enter(SEC2_AIR);
}

static u16 col_of_screen(void) { return (u16)((g_wx >> 3) - g_w0); }

/* 自機の上下の限り: 自機が掛かる列(左端・右端)の天井の一番低い所と床の一番高い所 */
static void terrain(void) {
    u16 c0 = (u16)(((g_wx + (u16)PX(g_pl.x)) >> 3) - g_w0), c1 = (u16)(c0 + 2);
    s16 top = 0, bot = 188;
    u16 c;
    if (g_sec < SEC2_SINK || g_sec == SEC2_OUT) { g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16; return; }
    for (c = c0; c <= c1; c++) {
        u8 k;
        if (c >= S2_COLS) continue;
        k = g_map[c];
        if (s2_top[k] > top) top = s2_top[k];
        if (s2_bot[k] < bot) bot = s2_bot[k];
    }
    g_pl_ymin = (s16)(top - g_scam);
    g_pl_ymax = (s16)(bot - 16 - g_scam);
    if (g_pl_ymin < 0) g_pl_ymin = 0;
    if (g_pl_ymax > PLAY_H - 16) g_pl_ymax = PLAY_H - 16;
}

void ovl_update(void) {
    sec_t++;
    switch (g_sec) {
    case SEC2_AIR:
        if ((u16)(g_wx - sec_wx) >= AIR_LEN) enter(SEC2_SINK);
        break;
    case SEC2_SINK: {
        u16 d = (u16)(g_wx - sec_wx);
        if (d >= SINK_LEN) { g_scam_q = 0; enter(g_calib ? SEC2_CENTER : SEC2_DOWN); }
        else {
            g_scam_q = (s16)(S_TOP * 16 + (s16)(d * 12u));    /* 192 行 / 256 ドット(× 16) */
            if (d > 128) { g_grav = GRAV_DIR; g_gx = 0; g_gy = 1; }   /* 沈む途中から重力 */
        }
        break;
    }
    case SEC2_DOWN:
        if (col_of_screen() >= S2_COL_FLIP) enter(SEC2_FLIP);
        break;
    case SEC2_FLIP:
        /* 警告の後、1/2 秒かけて画面が縦に一周する。回り終えたら重力は上向き */
        if (sec_t < 30) g_grav = GRAV_NONE;
        else if (sec_t < 62) g_roll = (u8)(g_roll + 8);
        else if (sec_t == 62) { g_roll = 0; g_grav = GRAV_DIR; g_gy = -1; }
        if (col_of_screen() + 8 >= S2_COL_ARENA1) enter(SEC2_MID);
        break;
    case SEC2_MID:
    case SEC2_BOSS:
        if (g_scroll_speed && !(sec_t & 7)) g_scroll_speed--;
        if (sec_t == 3600) g_boss_state = BS_DONE;
        if (g_boss_state == BS_DONE && !g_scroll_speed) {
            g_boss_state = BS_NONE; g_scroll_speed = 4;
            enter(g_sec == SEC2_MID ? SEC2_CENTER : SEC2_OUT);
        }
        break;
    case SEC2_CENTER:
        g_gcx = 128; g_gcy = 94;
        if (col_of_screen() + 8 >= S2_COL_ARENA2) enter(SEC2_BOSS);
        break;
    case SEC2_OUT:
        if (g_calib) { if (calib_out(sec_t)) return; break; }  /* 試験に合格 */
        if (sec_t == 60) { u8 i; for (i = 0; i < MAX_ESHOT; i++) g_eshot[i].on = 0; }
        if (g_scam_q > S_TOP * 16) g_scam_q -= 24;           /* 星空へ上がる */
        else if (sec_t > 120 && g_scroll_speed < 8 && !(sec_t & 15)) g_scroll_speed++;   /* 月の光の線へ加速 */
        if (sec_t == 200) {                                   /* 3 面へ(このあと 2 面のコードは呼ばれない) */
            g_scroll_speed = 4;
            stage_banner("STAGE 3 / OVERDRIVE CORRIDOR");
            ovl_request(OVL_S3);
            g_sec = SEC2_OUT + 1;                              /* = 3 面の空中戦(SEC3_AIR) */
        }
        break;
    default:
        break;
    }
    terrain();
}

void ovl_logic(void) { b2_update(); }
void ovl_draw(void) { b2_draw(); }
void ovl_vram(void) { b2_vram(); }

/* 地面の帯: 宇宙の色(色 1)と、KILOGRAM の重力レンズ(球の上と下の HR ポーリング帯) */
void ovl_bands(s16 sc, u16 nx) {
    s16 lt, lb;
    (void)nx;
    if (!g_calib) stage_ev_pal((s16)(0 - sc), space);         /* 試験では測定空間の白のまま */
    if (b2_lens(&lt, &lb)) {
        stage_ev_haze(lt, 24, b2_lens_tbl_up);
        stage_ev_haze(lb, 24, b2_lens_tbl_dn);
    }
}
