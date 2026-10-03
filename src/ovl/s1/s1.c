/* s1.c — 1 面のオーバレイ: 区間の進行・地図と短冊・屋上の砲台・都市の空の帯、ボスと Pawn への橋渡し(指示書 5.1)。
   空中戦 → 降下(R#23 の縦スクロールと横スクロールを同時に)→ 都市前半 → 中ボス → 都市後半 → ボス →
   上昇(縦スクロールで空へ抜ける)→ 2 面の空中戦(面の開始表示)。
   ★読み込まれるのは 1 面の空中戦が始まってから(stage_start が読み込みを頼む)。ovl_init で地図を読み、
     短冊の読み込みを始める。 */
#include "s1.h"
#include "ovl.h"
#include "rom.h"
#include "assets.h"
#include "city_data.h"

#define AIR1_LEN   512             /* 空中戦(ドット) */
#define DESC_LEN   256             /* 降下(この間に g_scam が -188 → 0) */
#define S_AIR      (-188)
#define S_AIR2     (-192)          /* 2 面の空中戦。★ブロック 23(s = -4..-1)が画面に出ない位置 */

static u16 sec_wx, sec_t;
static u8  turret_i;

static const u8 city_sky[6][3] = { { 2, 1, 2 }, { 3, 1, 3 }, { 4, 1, 3 }, { 5, 2, 3 }, { 6, 3, 3 }, { 7, 4, 2 } };
static const u8 city_sky_s[6] = { 0, 14, 40, 70, 100, 130 };

static void enter(u8 sec) {
    g_sec = sec; sec_wx = g_wx; sec_t = 0;
    switch (sec) {
    case SEC_AIR1: case SEC_AIR2: enemy_waves(WAVES_AIR); break;
    case SEC_CITY1: case SEC_CITY2: enemy_waves(WAVES_CITY); break;
    default: enemy_waves(WAVES_NONE); break;
    }
    if (sec == SEC_MID) boss_start(BOSS_LEGION);
    if (sec == SEC_CITY2) boss_prepare(BOSS_PLUMB);           /* ROTOR で下げ振りの絵を作り始める */
    if (sec == SEC_BOSS) boss_start(BOSS_PLUMB);
    if (sec == SEC_ASCENT) { stage_set_upper(UP_SKY); g_scroll_speed = 8; pawn_reset(); }
    if (sec == SEC_AIR2) {
        g_scroll_speed = 4; stage_banner("STAGE 2 / GRAVITY WELL");
        ovl_request(OVL_S2);                                  /* 2 面のオーバレイへ(このあと 1 面のコードは呼ばれない) */
    }
}

void ovl_init(void) {
    if (g_calib) {                                            /* 7 面の試験 01: 白い空間から都市の広場へ降りて PLUMB(2 個) */
        stage_reset_ground();
        g_w0 = (u16)((g_wx >> 3) - (CITY_COL_ARENA2 - 8 - 80));   /* 通路 16 列 + 降下 32 列 + 都市 32 列で広場 */
        rom_to_ram(ASSET_CITY_MAP_BANK, ASSET_CITY_MAP_OFF, g_map, CITY_COLS);
        g_maplen = CITY_COLS;
        stage_load_strips(ASSET_CITY_STRIPS_BANK, ASSET_CITY_STRIPS_OFF);
        stage_set_upper(UP_SKY);
        pawn_reset(); boss_reset();                           /* Pawn の絵は読まない(試験には出ない) */
        turret_i = CITY_NTURRET;                              /* 屋上の砲台は出さない */
        calib_begin(1);
        enter(SEC_AIR1);                                      /* 短い通路(短冊を読む間)→ 降下 → 都市後半(絵を作る) */
        return;
    }
    g_w0 = (u16)((g_wx + AIR1_LEN) >> 3);
    rom_to_ram(ASSET_CITY_MAP_BANK, ASSET_CITY_MAP_OFF, g_map, CITY_COLS);
    g_maplen = CITY_COLS;
    stage_load_strips(ASSET_CITY_STRIPS_BANK, ASSET_CITY_STRIPS_OFF);
    pawn_sheet_load(); pawn_reset(); boss_reset();
    turret_i = 0;
    enter(SEC_AIR1);
}

static u16 col_of_screen(void) { return (u16)((g_wx >> 3) - g_w0); }   /* 画面の左端の地図の列 */

void ovl_update(void) {
    sec_t++;
    switch (g_sec) {
    case SEC_AIR1:
        if ((u16)(g_wx - sec_wx) >= (g_calib ? 128u : AIR1_LEN)) enter(SEC_DESCENT);
        break;
    case SEC_DESCENT: {
        u16 d = (u16)(g_wx - sec_wx);
        if (d >= DESC_LEN) { g_scam_q = 0; enter(g_calib ? SEC_CITY2 : SEC_CITY1); }
        else g_scam_q = (s16)(S_AIR * 16 + (s16)((d * 47u) >> 2));   /* 188 行 / 256 ドット = 47/64(× 16) */
        break;
    }
    case SEC_CITY1:
        if (col_of_screen() + 8 >= CITY_COL_ARENA1) enter(SEC_MID);
        break;
    case SEC_MID:
    case SEC_BOSS:      /* スクロールを止めて戦う。倒すか時間切れ(60 秒)で先へ */
        if (g_scroll_speed && !(sec_t & 7)) g_scroll_speed--;
        if (sec_t == 3600) boss_leave();
        if (g_boss_state == BS_DONE && !g_scroll_speed) {
            g_boss_state = BS_NONE;
            if (g_sec == SEC_MID) { g_scroll_speed = 4; enter(SEC_CITY2); }
            else enter(SEC_ASCENT);
        }
        break;
    case SEC_CITY2:
        if (col_of_screen() + 8 >= CITY_COL_ARENA2) enter(SEC_BOSS);
        break;
    case SEC_ASCENT:
        if (g_calib) { if (calib_out(sec_t)) return; break; }  /* 試験に合格: 判定文 → 次の試験へ */
        g_scam_q -= 24;                                       /* 1.5 行/フレーム */
        if (g_sky_t < 16 && !(sec_t & 7)) g_sky_t++;
        if (g_scam_q <= S_AIR2 * 16) { g_scam_q = S_AIR2 * 16; enter(SEC_AIR2); }
        break;
    default:
        break;
    }

    /* 街路の Pawn(都市の前半・後半で出す。広場ではもう出さないが、いるものは歩き続ける) */
    if (g_sec >= SEC_CITY1 && g_sec <= SEC_BOSS && !g_slow && !g_calib)
        pawn_update((u8)((g_sec == SEC_CITY1 || g_sec == SEC_CITY2) && g_scam == 0));

    /* 屋上の砲台: 列が画面の右端に来たら置く */
    while (turret_i < CITY_NTURRET) {
        u16 tx = (u16)((g_w0 + city_turret_col[turret_i]) * 8u + 4u);
        if ((s16)(tx - g_wx) > 264) break;
        if ((s16)(tx - g_wx) > 0) enemy_spawn_spirit(tx, city_turret_row[turret_i]);
        turret_i++;
    }
}

void ovl_logic(void) { boss_update(); }
void ovl_draw(void) { boss_draw(); }
void ovl_vram(void) { boss_vram(); }

/* 都市の空の帯と、LEGION の揺れの帯(横のずれ)を行の順に混ぜる */
void ovl_bands(s16 sc, u16 nx) {
    s16 bl[5]; s8 bo[5];
    u8 i, nb = boss_bands(bl, bo), kb = 0;
    for (i = 0; i < 6; i++) {
        s16 l = (s16)(city_sky_s[i] - sc);
        while (kb < nb && bl[kb] < l) { stage_ev_scroll(bl[kb], (u16)(nx - bo[kb])); kb++; }
        stage_ev_pal(l, city_sky[i]);
    }
    while (kb < nb) { stage_ev_scroll(bl[kb], (u16)(nx - bo[kb])); kb++; }
}
