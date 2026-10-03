/* s4.c — 4 面「層流」のオーバレイ: 区間・雲の帯・陽炎(指示書 5.4)。
   宇宙 → ガス惑星の大気へ降りる → 大気上層(6 帯)→ 雲海(12 帯、一部は逆向き)→ 陽炎帯(下 40 ラインが波打つ)
   → 中ボス VERNIER → 大河(RULER が帯をまたいで泳ぐ)→ ボス STRATUM → 雲が一斉に同じ向きへ。
   ・雲の絵は横 256 周期の 1 枚(短冊 32 本)。降りる間に列の流し込みで表示リング(512 幅)を埋め、降りきったら
     世界のスクロールを止める(g_scroll_speed = 0)。以後の動きは帯ごとの R#26/R#27 だけ = VDP の仕事はゼロ。
     絵が周期 256 なので、帯の横位置はリングのどこでもよい。
   ・帯の境は雲の層の頭の暗い縁(2 行)。ラスターの切り替えの行の乱れを隠す。
   ・帯ごとに色 1(空)をラスターで替えて、上の紫から下の橙へ。
   ★読み込まれるのは 4 面の空中戦が始まってから(3 面のオーバレイが頼む)。 */
#include "s4.h"
#include "ovl.h"
#include "vdp.h"
#include "rom.h"
#include "assets.h"
#include "s4_data.h"
#include "ghost.h"

#define AIR_LEN  256
#define SINK_LEN 256
#define S_TOP    (-192)
#define HAZE_LINE 149              /* 陽炎帯(画面の下 32 ライン)。HUD の項目(188 行)の手前 RAS_GAP まで */
#define HAZE_N    32

static u16 sec_wx, sec_t;
static u16 bq[S4_NL];              /* 層ごとの流れ(1/4 ドット) */
static s8  bv[S4_NL];              /* 層ごとの速さ(1/4 ドット/フレーム、負 = 逆向き) */
static u16 hold[S4_NL];            /* ボスが決めた横位置(0xFFFF = 流れのまま) */
static u16 bx[S4_NL];              /* 今の横位置(流れか押さえ。帯を組むたびに求め直さない) */
static u8  haze_tbl[HAZE_N * 2];
static u8  flash_t;                /* 稲妻 */
static u8  sky[S4_NL][3];

/* 区間ごとの層の速さ(1/4 ドット/フレーム) */
static const s8 v_upper[S4_NL] = { 1, 1, 2, 2, 3, 3, 4, 4, 6, 6, 8, 8 };              /* 2 層ずつ = 6 帯 */
static const s8 v_sea[S4_NL]   = { 2, 3, -2, 4, 5, -3, 6, -4, 7, 8, -5, 10 };          /* 12 帯、逆向きあり */
static const s8 v_calm[S4_NL]  = { 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4 };
static const s8 v_river[S4_NL] = { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 };
static const s8 v_out[S4_NL]   = { 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 };
static const s8 *v_want;

/* 空の色(上の紫 → 下の橙) */
static const u8 sky_dusk[S4_NL][3] = {
    { 1, 0, 2 }, { 1, 0, 3 }, { 2, 0, 3 }, { 2, 1, 3 }, { 3, 1, 3 }, { 3, 1, 2 },
    { 4, 1, 2 }, { 4, 2, 2 }, { 5, 2, 1 }, { 5, 2, 1 }, { 6, 3, 1 }, { 6, 3, 1 } };

static void enter(u8 sec) {
    g_sec = sec; sec_wx = g_wx; sec_t = 0;
    switch (sec) {
    case SEC4_AIR: case SEC4_UPPER: case SEC4_SEA: case SEC4_HAZE: enemy_waves(WAVES_AIR); break;
    /* 大河は RULER の砲台が相手(編隊も出すと 12 帯 + 体節の当たりと重なって遅れた) */
    default: enemy_waves(WAVES_NONE); break;
    }
    if (sec == SEC4_UPPER) v_want = v_upper;
    if (sec == SEC4_SEA || sec == SEC4_HAZE) v_want = v_sea;
    if (sec == SEC4_SEA) b4_load_face();                      /* STRATUM の顔は静かな区間のうちにページ 2 へ */
    if (sec == SEC4_MID) { v_want = v_calm; b4_start(B4_VERNIER); }
    if (sec == SEC4_RIVER) { v_want = v_river; b4_start(B4_RULER); }
    if (sec == SEC4_BOSS) { v_want = v_calm; b4_start(B4_STRATUM); }
    if (sec == SEC4_OUT) v_want = v_out;
}

void ovl_init(void) {
    u8 i;
    stage_reset_ground();
    g_w0 = (u16)((g_wx + AIR_LEN) >> 3);
    rom_to_ram(ASSET_S4_MAP_BANK, ASSET_S4_MAP_OFF, g_map, S4_COLS);
    g_maplen = S4_COLS;
    stage_load_strips(ASSET_S4_STRIPS_BANK, ASSET_S4_STRIPS_OFF);
    g_boss_state = BS_NONE; g_grav = GRAV_NONE; g_col_rev = 0;
    for (i = 0; i < S4_NL; i++) { bq[i] = 0; bv[i] = 0; hold[i] = 0xFFFF; bx[i] = g_wx; }
    v_want = 0; flash_t = 0;
    enter(SEC4_AIR);
}

u16 s4_band_x(u8 layer) { return bx[layer]; }
void s4_band_hold(u8 layer, u16 x) {
    if (x == 0xFFFF) {
        if (hold[layer] != 0xFFFF) bq[layer] = (u16)((hold[layer] - bg_layer_x(2)) << 2);   /* 今の位置から流れに戻す */
    } else bx[layer] = x;
    hold[layer] = x;
}
s16 s4_ring_sx(u16 rx, u8 layer) {
    s16 d = (s16)((rx - s4_band_x(layer)) & 511u);
    return d >= 256 ? (s16)(d - 512) : d;
}
/* 雲の絵はリングの x とページ 3 の x が 1 対 1: 世界の列 c の短冊は (c - g_w0) & 31、リングの x は c * 8 なので、
   ページ 3 の x = (rx - g_w0 * 8) & 255。★g_w0 を引き忘れると、32 列の倍数でない分だけずれた絵で戻していた */
void s4_ring_restore(u16 rx, s16 s, u16 w, u8 h) {
    while (w) {
        u16 sx = (u16)(rx - (g_w0 << 3)) & 255u, n = (u16)(256u - sx);
        if (n > w) n = w;
        if (n > 254) n = 254;                                 /* ring_cmd の幅は u8(偶数に丸める) */
        ring_cmd(sx, (u16)(768 + s), (s16)(rx - g_wx), (s16)(s - g_scam), (u8)n, h, 0, 0);
        rx += n; w -= n;
    }
}

static void palette(void) {
    vdp_set_pal(3, 3, 2, 1); vdp_set_pal(4, 5, 3, 2); vdp_set_pal(5, 6, 5, 4);
    vdp_set_pal(6, 1, 0, 1); vdp_set_pal(7, 7, 7, 6); vdp_set_pal(2, 1, 0, 1);
}

static void flow(void) {
    u8 i;
    if (v_want && !(sec_t & 3))                               /* 速さはゆっくり替える(帯がいきなり跳ねない) */
        for (i = 0; i < S4_NL; i++) { if (bv[i] < v_want[i]) bv[i]++; else if (bv[i] > v_want[i]) bv[i]--; }
    {
        u16 nx = bg_layer_x(2);
        for (i = 0; i < S4_NL; i++) {
            bq[i] += (u16)(s16)bv[i];
            if (hold[i] == 0xFFFF) bx[i] = (u16)(nx + (bq[i] >> 2));
        }
    }
}

static void haze(void) {
    u8 i, ph = (u8)(sec_t << 1);
    u16 x = s4_band_x(8);                                     /* R#26 は上の帯(層 8)のまま(FINE は R#27 だけ書く) */
    for (i = 0; i < HAZE_N; i++) {
        s8 sv = g_sin64[(u8)(ph + i * 5) & 63];
        u8 a = (u8)(sv + 128) >> 5;                           /* 0..7 */
        if (sec_t < 64) a = (u8)((a * sec_t) >> 6);           /* 揺らぎは少しずつ */
        haze_tbl[i * 2] = VDP_R26(x); haze_tbl[i * 2 + 1] = a;
    }
}

void ovl_update(void) {
    u8 i;
    sec_t++;
    switch (g_sec) {
    case SEC4_AIR:
        if (g_scroll_speed > 4 && !(sec_t & 1)) g_scroll_speed--;    /* 3 面の射出の速さから */
        if ((u16)(g_wx - sec_wx) >= AIR_LEN) { palette(); enter(SEC4_SINK); }
        break;
    case SEC4_SINK: {
        u16 d = (u16)(g_wx - sec_wx);
        if (d >= SINK_LEN && !stage_busy()) {                 /* リングが雲で埋まってから止める */
            g_scam_q = 0; g_scroll_speed = 0;
            for (i = 0; i < S4_NL; i++) bv[i] = 4;            /* 世界の流れ(1 ドット)を帯へ引き継ぐ */
            enter(SEC4_UPPER);
        } else if (d < SINK_LEN) g_scam_q = (s16)(S_TOP * 16 + (s16)(d * 12u));
        break;
    }
    case SEC4_UPPER: if (sec_t >= 480) enter(SEC4_SEA); break;
    case SEC4_SEA:   if (sec_t >= 1200) enter(SEC4_HAZE); break;
    case SEC4_HAZE:  if (sec_t >= 480) enter(SEC4_MID); break;
    case SEC4_MID:
    case SEC4_RIVER:
    case SEC4_BOSS:
        if (sec_t == 5400) g_boss_state = BS_DONE;            /* 長引いたら先へ */
        if (g_boss_state == BS_DONE) {
            g_boss_state = BS_NONE;
            enter(g_sec == SEC4_MID ? SEC4_RIVER : g_sec == SEC4_RIVER ? SEC4_BOSS : SEC4_OUT);
        }
        break;
    case SEC4_OUT:                                            /* 雲が一斉に流れる → 世界も動かして宇宙へ上がる */
        if (g_scroll_speed < 4 && !(sec_t & 7)) g_scroll_speed++;
        if (sec_t > 180 && g_scam_q > S_TOP * 16) g_scam_q -= 24;
        if (sec_t == 420) {                                   /* 5 面へ(このあと 4 面のコードは呼ばれない) */
            stage_banner("STAGE 5 / DREADNOUGHT NORM");
            ovl_request(OVL_S5);
            g_sec = SEC4_OUT + 1;                              /* = 5 面の空中戦(SEC5_AIR) */
        }
        break;
    default:
        break;
    }
    flow();
    if (g_sec == SEC4_HAZE) haze();
    /* 稲妻: 雲海と陽炎帯でときどき空が光る */
    if (flash_t) flash_t--;
    else if ((g_sec == SEC4_SEA || g_sec == SEC4_HAZE) && rnd8() < 2) flash_t = 4;
    for (i = 0; i < S4_NL; i++) {
        const u8 *c = sky_dusk[i];
        if (flash_t && i >= 6) { sky[i][0] = 6; sky[i][1] = 6; sky[i][2] = 7; }
        else { sky[i][0] = c[0]; sky[i][1] = c[1]; sky[i][2] = c[2]; }
    }
    g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16;
}

void ovl_logic(void) { b4_update(); }
void ovl_draw(void) { b4_draw(); }
void ovl_vram(void) { b4_vram(); }

void ovl_bands(s16 sc, u16 nx) {
    /* ★ラスターの項目 1 つで割込みが 2〜3 ライン、陽炎は 1 ラインごとに CPU を使う(32 ライン)。陽炎帯では
       上を 2 層ずつにして合計を抑える(12 帯 + 40 ラインの陽炎では毎フレーム遅れた) */
    u8 i, step = (g_sec == SEC4_UPPER || g_sec == SEC4_HAZE) ? 2 : 1, nl = S4_NL;
    (void)nx;
    if (g_sec < SEC4_SINK) return;
    if (b4_bands(sc)) return;
    if (g_sec == SEC4_HAZE) nl = 9;                          /* 層 9 から下は陽炎(R#26 は層 8 の項目のまま) */
    for (i = 0; i < nl; i += step) {
        s16 l = (s16)(s4_layer[i] - sc);
        if (l >= PLAY_H) break;
        stage_ev_pal(l, sky[i]);
        stage_ev_scroll(l, s4_band_x(i));
    }
    if (nl < S4_NL) {
        stage_ev_haze((s16)(HAZE_LINE - sc), HAZE_N, haze_tbl);
    }
}
