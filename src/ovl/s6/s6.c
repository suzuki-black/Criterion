/* s6.c — 6 面「日時計要塞」のオーバレイ: 区間・画面分割・影・当たり(指示書 5.6)。
   宇宙 → 衛星の上へ(カメラ s = -120)。上 48 ラインを SCREEN 12(YJK の惑星、ゆっくり横へ)、下を SCREEN 5 の
   プレイエリアに分割する → 外壁(歯車・砂粒虫)→ 影の区間(影の帯の中は色を暗い組に替え、敵は目だけが光る)→
   中ボス HOURGLASS → 文字盤(数字の砲台)→ ボス GNOMON(針を LINE で毎フレーム描き替え)→ 時刻が止まる。
   ・分割(タイトルと同じ): 帰線の 244 行で SCREEN 12 へ、48 行で SCREEN 5 へ戻す(RAS_MARK)。★上の帯の間は CPU の
     VRAM の書き込みも VDP のコマンドも化けるので、VRAM の仕事は 48 行より後(main.c の wait_vram_window)だけ。
     列の流し込みの「待たない」呼び出しも止める(stage_pump_nb)。
   ・YJK の絵はページ 0 と 2 の 0..47 行 = リングの 0..47 行(s ≡ 68..115)。プレイエリアは s = -72..67 なので
     そこは見えない。列と描き替えでそこを描かないよう g_blk_skip = 6。
   ・色 6 は空の色(ブロック 23)。
   ★読み込まれるのは 6 面の空中戦が始まってから(5 面のオーバレイが頼む)。 */
#include "s6.h"
#include "ovl.h"
#include "vdp.h"
#include "rom.h"
#include "assets.h"
#include "s6_data.h"
#include "spr.h"
#include "sys.h"

#define AIR_LEN  1200              /* 空中戦の残り(ドット。★4〜6 秒は短すぎた。約 20 秒に。M7 の後) */
#define S_TOP    (-192)

static u16 sec_wx, sec_t;
static u8  yjk_row;                /* 読んだ YJK の行(0..95: 偶数バイト 48 行 → 奇数バイト 48 行) */
static u16 yjk_x;                  /* 惑星の横位置(1/8 ドット) */
static u8  yjk_stop;
s16 s6_shadow_a, s6_shadow_b;
static u8  regs_top[2][14], regs_n[2][12];

static const u8 sky[3] = { 1, 1, 3 };
/* 影の中の暗い組(色 1 空 / 4・5・7 外壁 / 12 敵の体) */
static const u8 sh_idx[5] = { 1, 4, 5, 7, 12 };
static const u8 sh_dark[5][3] = { { 0, 0, 1 }, { 1, 1, 1 }, { 2, 1, 1 }, { 2, 2, 1 }, { 0, 0, 0 } };
static const u8 sh_lit[5][3] = { { 1, 1, 3 }, { 5, 4, 2 }, { 6, 5, 3 }, { 7, 7, 5 }, { 6, 5, 2 } };

s16 s6_sx(u16 col) { return (s16)((u16)((g_w0 + col) << 3) - g_wx); }
s16 s6_sy(s16 s) { return (s16)(s - g_scam); }
u16 s6_col_at(s16 sx) { return (u16)(((u16)(g_wx + (u16)sx) >> 3) - g_w0); }
static u16 col_of_screen(void) { return s6_col_at(0); }

static void palette(void) {
    vdp_set_pal(6, 1, 1, 3);                                  /* = 空 */
    vdp_set_pal(2, 1, 1, 1); vdp_set_pal(3, 3, 2, 1); vdp_set_pal(4, 5, 4, 2); vdp_set_pal(5, 6, 5, 3); vdp_set_pal(7, 7, 7, 5);
}

static void enter(u8 sec) {
    g_sec = sec; sec_wx = g_wx; sec_t = 0;
    switch (sec) {
    case SEC6_AIR: enemy_waves(WAVES_SKY); break;
    case SEC6_WALL: case SEC6_SHADOW: enemy_waves(WAVES_AIR); break;
    case SEC6_DIAL: enemy_waves(WAVES_CITY); break;
    default: enemy_waves(WAVES_NONE); break;
    }
    if (sec == SEC6_MID) b6_mid_start();
    if (sec == SEC6_BOSS) b6_boss_start();
}

void ovl_init(void) {
    stage_reset_ground();
    g_w0 = (u16)((g_wx + AIR_LEN) >> 3);
    rom_to_ram(ASSET_S6_MAP_BANK, ASSET_S6_MAP_OFF, g_map, S6_COLS);
    g_maplen = S6_COLS;
    stage_load_strips(ASSET_S6_STRIPS_BANK, ASSET_S6_STRIPS_OFF);
    g_blk_skip = 6;                                           /* リングの 0..47 行は YJK */
    g_bands_all = 1;
    g_boss_state = BS_NONE; g_grav = GRAV_NONE; g_col_rev = 0;
    yjk_row = 0; yjk_x = 0; yjk_stop = 0; s6_shadow_a = s6_shadow_b = 0;
    palette();
    b6_init();
    if (g_calib) {                                            /* 7 面の試験 06: 分割なしで指針の間へ降りて GNOMON(針 2 本) */
        u16 c;
        g_blk_skip = 0;
        g_w0 = (u16)((g_wx >> 3) - (S6_COL_BOSS - 48));
        for (c = S6_COL_BOSS - 56; c < S6_COL_BOSS; c++) g_map[c] = (u8)(23 + (c & 3));   /* 手前も床 */
        calib_begin(6);
        g_scroll_speed = 4;
        enter(SEC6_DIAL);
        return;
    }
    enter(SEC6_AIR);
}

static void terrain(void) {
    u16 c0 = s6_col_at(PX(g_pl.x)), c;
    s16 hi = PLAY_H - 16, lo = g_split_n ? SPLIT_H : 0;
    for (c = c0; c <= (u16)(c0 + 2); c++) {
        s16 t;
        if (c >= S6_COLS) continue;
        t = (s16)(s6_top[g_map[c]] - 16 - g_scam);
        if (t < hi) hi = t;
    }
    if (hi < lo) hi = lo;
    g_pl_ymin = lo; g_pl_ymax = hi > PLAY_H - 16 ? PLAY_H - 16 : hi;
}

void ovl_update(void) {
    u16 c = col_of_screen();
    sec_t++;
    if (!yjk_stop && !(sec_t & 7)) yjk_x++;
    if (g_calib && g_sec != SEC6_OUT && g_scam_q < S6_CAM * 16) g_scam_q += 16;   /* 試験: 指針の間の高さへ */
    switch (g_sec) {
    case SEC6_AIR:                                            /* 宇宙(短冊を読む間)→ 衛星の上へ */
        if (g_scroll_speed != 4 && !(sec_t & 3)) g_scroll_speed += g_scroll_speed < 4 ? 1 : -1;
        if ((u16)(g_wx - sec_wx) >= AIR_LEN) enter(SEC6_SINK);
        break;
    case SEC6_SINK:                                           /* カメラを下ろし、YJK を読んでから帯を伸ばす */
        if (g_scam_q < S6_CAM * 16) g_scam_q += 16;
        if (g_scam_q >= S6_CAM * 16 && yjk_row >= 96) {
            if (g_split_n < SPLIT_H) g_split_n = (u8)(g_split_n < 8 ? 8 : g_split_n + 2);
            else enter(SEC6_WALL);
        }
        break;
    case SEC6_WALL:   if (c + 32 >= S6_COL_SHADOW + 8) enter(SEC6_SHADOW); break;
    case SEC6_SHADOW:                                         /* 指針の影が上から下へ横切っていく */
        s6_shadow_a = (s16)(SPLIT_H - 60 + (sec_t >> 2));
        s6_shadow_b = (s16)(s6_shadow_a + 70);
        if (s6_shadow_a > PLAY_H) { s6_shadow_a = s6_shadow_b = 0; }
        if (c + 24 >= S6_COL_MID) { s6_shadow_a = s6_shadow_b = 0; g_scroll_speed = 0; enter(SEC6_MID); }
        break;
    case SEC6_MID:
    case SEC6_BOSS:
        if (sec_t == 5400) g_boss_state = BS_DONE;
        if (g_boss_state == BS_DONE) {
            g_boss_state = BS_NONE;
            if (g_sec == SEC6_MID) { g_scroll_speed = 4; enter(SEC6_DIAL); }
            else { yjk_stop = 1; g_scroll_speed = 4; enter(SEC6_OUT); }   /* 時刻が止まる: 惑星の絵も止まる */
        }
        break;
    case SEC6_OUT:                                            /* 時刻が止まる → 帯を畳み、宇宙へ上がって 7 面(母艦)へ */
        if (g_calib) { if (calib_out(sec_t)) return; break; }
        if (sec_t > 120 && g_split_n) g_split_n = (u8)(g_split_n > 9 ? g_split_n - 2 : 0);
        if (sec_t > 120 && !g_split_n) { g_blk_skip = 0; if (g_scam_q > S_TOP * 16) g_scam_q -= 16; }
        if (sec_t > 120 && !g_split_n && g_scam_q <= S_TOP * 16) {
            g_scroll_speed = 4;
            stage_banner("STAGE 7 / CALIBRATION HALL");
            ovl_request(OVL_S7);
            g_sec = SEC6_OUT + 1;                              /* = 7 面の空中戦(SEC7_AIR) */
            return;
        }
        break;
    case SEC6_DIAL:                                           /* 指針の間: リングのページ 0 = 画面になる位置で止める(LINE の座標) */
        if (c + 8 >= S6_COL_BOSS && (g_wx & 511u) == 0) { g_scroll_speed = 0; enter(SEC6_BOSS); }
        break;
    default:
        break;
    }
    if (g_sec >= SEC6_WALL) terrain();
    else { g_pl_ymin = g_split_n; g_pl_ymax = PLAY_H - 16; }
}

void ovl_logic(void) { b6_update(); }
void ovl_draw(void) { b6_draw(); }

void ovl_vram(void) {
    /* YJK の惑星: 偶数バイト → ページ 0、奇数バイト → ページ 2 の 0..47 行(リングの 0..47 行は見えていない) */
    extern u16 g_frame_t0;
    if (yjk_row < 96 && g_scam == S6_CAM && (u16)(sys_timer() - g_frame_t0) < 150u * 16u) {   /* 1 行 128 バイト ≒ 1.2ms */
        u8 n;
        for (n = 0; n < 2 && yjk_row < 96; n++, yjk_row++) {
            u8 r = (u8)(yjk_row < 48 ? yjk_row : yjk_row - 48);
            if (yjk_row < 48) rom_to_vram(ASSET_YJK_EVEN_BANK, (u16)(ASSET_YJK_EVEN_OFF + r * 128u), 0, (u16)(r * 128u), 128);
            else rom_to_vram(ASSET_YJK_ODD_BANK, (u16)(ASSET_YJK_ODD_OFF + r * 128u), 1, (u16)(r * 128u), 128);
        }
    }
    b6_vram();
}

void ovl_bands(s16 sc, u16 nx) {
    u8 bi = ras_back_index(), n = g_split_n;
    RasEntry *e;
    stage_ev_pal(0, sky);
    stage_ev_scroll(0, nx);
    if (n) {
        /* 帰線の 244 行: SCREEN 12(YJK)、ページ 0(インターリーブ)、R#23 = 0、スプライトなし */
        u8 *rt = regs_top[bi], *rn = regs_n[bi];
        u16 yx = yjk_x >> 3;
        rt[0] = 0; rt[1] = 0x1E; rt[2] = 25; rt[3] = 0x0A; rt[4] = 2; rt[5] = 0x1F;
        rt[6] = 8; rt[7] = 0x2A; rt[8] = 23; rt[9] = 0; rt[10] = 5; rt[11] = spr_regs.r5a; rt[12] = 11; rt[13] = 3;
        e = stage_ev_raw(244, RAS_REGS | RAS_SCROLL);
        if (e) { e->haze = rt; e->haze_n = 7; e->r26 = (u8)(VDP_R26(yx) & 31); e->r27 = VDP_R27(yx); }
        /* n 行: SCREEN 5 の表示リングへ戻す(ここから VRAM の窓) */
        rn[0] = 0; rn[1] = 0x16; rn[2] = 25; rn[3] = 0x03; rn[4] = 8; rn[5] = 0x28;
        rn[6] = 2; rn[7] = 0x3F; rn[8] = 23; rn[9] = (u8)(g_vscroll + g_roll); rn[10] = 5; rn[11] = spr_regs.r5a;
        e = stage_ev_raw(n, RAS_REGS | RAS_SCROLL | RAS_PAL | RAS_MARK);
        if (e) {
            e->haze = rn; e->haze_n = 6; e->r26 = VDP_R26(nx); e->r27 = VDP_R27(nx);
            e->pidx = 1; e->prb = (u8)((sky[0] << 4) | sky[2]); e->pg = sky[1];
        }
    }
    (void)sc;
    /* 影: 帯の上端から 6 行ずつ 1 色ずつ暗くし、下端から 1 色ずつ戻す(半影) */
    if (s6_shadow_b > s6_shadow_a) {                          /* ★分割の行(n)より上へは並べられない */
        u8 i;
        s16 lim = (s16)(n + RAS_GAP);
        for (i = 0; i < 5; i++) { s16 l = (s16)(s6_shadow_a + i * 6); stage_ev_pal_i(l < lim ? lim : l, sh_idx[i], sh_dark[i]); }
        for (i = 0; i < 5; i++) { s16 l = (s16)(s6_shadow_b + i * 6); stage_ev_pal_i(l < lim ? lim : l, sh_idx[i], sh_lit[i]); }
    }
    b6_bands();
}
