/* s7.c — 7 面「校正室」のオーバレイ: 測定空間の空中戦と、試験の進行、最終試験 LIBRA(指示書 5.7)。
   ・測定空間は高い空(s < 0)を白い方眼にしたもの(stage.c の g_hall)。カメラは s = -192 のまま、地面は出さない。
   ・試験 01..06 は過去の面のオーバレイを g_calib つきで読み直して、そのボスの再校正版と戦う(各面の ovl_init が
     g_calib を見て広場へ直行し、倒したら calib_out で次の試験を頼む)。試験 07(LIBRA)でこのオーバレイに戻る。
   ★読み込まれるのは 6 面のあと(g_calib = 0)と、試験 06 のあと(g_calib = 7)。 */
#include "s7.h"
#include "ovl.h"
#include "vdp.h"

#define AIR_T 1200                 /* 測定空間の空中戦(フレーム) */

static u16 sec_t;

static void enter(u8 sec) {
    g_sec = sec; sec_t = 0;
    enemy_waves(sec == SEC7_AIR ? WAVES_AIR : WAVES_NONE);
    if (sec == SEC7_LIBRA) b7_start();
}

void ovl_init(void) {
    stage_reset_ground();
    stage_set_upper(UP_SKY);
    g_hall = 1;
    g_boss_state = BS_NONE; g_grav = GRAV_NONE; g_col_rev = 0;
    b7_init();
    if (g_calib == 7) { calib_begin(7); enter(SEC7_LIBRA); }
    else enter(SEC7_AIR);
}

void ovl_update(void) {
    sec_t++;
    if (g_scam_q > -192 * 16) g_scam_q -= 16;                 /* 測定空間(地面を出さない高さ) */
    if (g_sec != SEC7_LIBRA && g_scroll_speed != 4 && !(sec_t & 3)) g_scroll_speed += g_scroll_speed < 4 ? 1 : -1;
    switch (g_sec) {
    case SEC7_AIR:
        if (sec_t == AIR_T) { g_calib = 1; ovl_request(OVL_S1); return; }   /* 試験 01 へ */
        break;
    case SEC7_LIBRA:
        if (sec_t == 5400) g_boss_state = BS_DONE;
        if (g_boss_state == BS_DONE) { g_boss_state = BS_NONE; enter(SEC7_OUT); }
        break;
    case SEC7_OUT:                                            /* 白い空間が黒く反転し、縦穴が開く → 8 面 */
        b7_invert(sec_t);
        if (sec_t == 180) {
            g_calib = 0;
            stage_banner("STAGE 8 / THE PROTOTYPE");
            ovl_request(OVL_S8);
            g_sec = SEC7_OUT + 1;                              /* = 8 面の空中戦(SEC8_AIR) */
            return;
        }
        break;
    default:
        break;
    }
    g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16;
}

void ovl_logic(void) { b7_update(); }
void ovl_draw(void) { b7_draw(); }
void ovl_vram(void) { b7_vram(); }
void ovl_bands(s16 sc, u16 nx) { (void)sc; (void)nx; b7_bands(); }
