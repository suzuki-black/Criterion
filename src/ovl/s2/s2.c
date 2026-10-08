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
#include "snd.h"
#include "snd_data.h"
#include "spr.h"

#define AIR_LEN  1200              /* 空中戦の残り(ドット。★4〜6 秒は短すぎた。約 20 秒に。M7 の後) */
#define SINK_LEN 256               /* 沈む(g_scam が -192 → 0) */
#define S_TOP    (-192)

static u16 sec_wx, sec_t;
static const u8 space[3] = { 0, 0, 1 };

/* ---- 渦(重力・中心の区間。M7 の後): 地図に固定した 3 つの渦。近づいたときだけ引く(player.c の GRAV_CENTER)。
   ★前は画面の 1 点へ距離に比例して引いていて、自機が振り子のように回り続け、向きが毎フレーム変わって見えた
   (ゴーファーの野望の重力面にならい、渦を見せて、近づくと引かれる形に) ---- */
#define PAT_WELL 44                /* 渦の絵 4 コマ(44..47。面ごとの絵の置き場) */
#define NWELL 3
static const u16 well_col[NWELL] = { 296, 340, 384 };
static const u8  well_y[NWELL]   = { 52, 112, 62 };      /* 左上(通り道は 26..162 行) */
static const u8 well_pat[4][32] = {
    { 0x00, 0x0C, 0x10, 0x30, 0x20, 0x21, 0x21, 0x23, 0x33, 0x11, 0x0F, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xF0, 0x88, 0xCC, 0xC4, 0x84, 0x84, 0x04, 0x0C, 0x08, 0x30, 0x00 },
    { 0x00, 0x03, 0x06, 0x08, 0x18, 0x10, 0x11, 0x13, 0x0B, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xD0, 0xC8, 0x88, 0x08, 0x18, 0x10, 0x60, 0xC0, 0x00 },
    { 0x00, 0x00, 0x01, 0x03, 0x04, 0x04, 0x0D, 0x07, 0x07, 0x01, 0x40, 0x40, 0x30, 0x1F, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x0C, 0x02, 0x02, 0x80, 0xE0, 0xE0, 0xB0, 0x20, 0x20, 0xC0, 0x80, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x43, 0x42, 0x43, 0x43, 0x43, 0x61, 0x20, 0x18, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x18, 0x04, 0x86, 0xC2, 0xC2, 0xC2, 0x42, 0xC2, 0x00, 0x00, 0x00, 0x00 },
};
/* ★パレットアニメ(M7 の後): 残骸の赤い灯(色 2)は 1 秒に 1 回点き、星(色 3..5)は位相をずらして瞬く。
   空中戦と沈む間は雲の色なので、宇宙の区間(DOWN〜CENTER)だけ。KILOGRAM は 2..5 を自分の色にする */
static const u8 twk[8] = { 2, 3, 5, 7, 6, 4, 3, 2 };
static void twinkle(void) {
    u8 i, t = (u8)sec_t;
    if (t & 3) return;
    for (i = 0; i < 3; i++) { u8 v = twk[(u8)((t >> 2) + i * 3) & 7]; vdp_set_pal((u8)(3 + i), v, v, 7); }
    if ((u8)(t & 63) < 12) vdp_set_pal(2, 7, 1, 1); else vdp_set_pal(2, 2, 0, 1);
}

static void warn(const char *s) { stage_banner(s); pcm_req(PCM_V02); }   /* 重力が替わる予告(帯と声) */

static s16 well_sx(u8 i) { return (s16)((s16)(well_col[i] - (u16)((g_wx >> 3) - g_w0)) * 8 - (s16)(g_wx & 7)); }

/* 中心の区間: 自機にいちばん近い画面の中の渦を、重力の中心にする */
static void wells(void) {
    u8 i, best = 0xFF;
    s16 bd = 0x7FFF;
    for (i = 0; i < NWELL; i++) {
        s16 sx = well_sx(i), d = (s16)(sx - PX(g_pl.x));
        if (sx < -16 || sx > 256) continue;
        if (d < 0) d = -d;
        if (d < bd) { bd = d; best = i; }
    }
    if (best == 0xFF) { g_grav = GRAV_NONE; return; }
    g_grav = GRAV_CENTER; g_gcx = (s16)(well_sx(best) + 8); g_gcy = (s16)(well_y[best] + 8);
}

static void enter(u8 sec) {
    g_sec = sec; sec_wx = g_wx; sec_t = 0;
    switch (sec) {
    case SEC2_AIR: enemy_waves(WAVES_SKY); break;
    case SEC2_SINK: case SEC2_DOWN: case SEC2_FLIP: case SEC2_CENTER: enemy_waves(WAVES_AIR); break;
    default: enemy_waves(WAVES_NONE); break;
    }
    if (sec == SEC2_DOWN) warn("WARNING  GRAVITY  DOWN");    /* ★重力は予告の後(DOWN の 90 フレーム目)。M7 の後 */
    if (sec == SEC2_FLIP) warn("WARNING  GRAVITY  REVERSE");
    if (sec == SEC2_MID) { g_grav = GRAV_NONE; b2_start(B2_CW); }
    if (sec == SEC2_CENTER) { g_grav = GRAV_NONE; if (!g_calib) { b2_prepare(); warn("WARNING  GRAVITY  WELLS"); } }
    if (sec == SEC2_BOSS) { g_grav = GRAV_NONE; b2_start(B2_KG); }
    if (sec == SEC2_OUT) {
        u8 i;
        g_grav = GRAV_NONE; g_scroll_speed = 4;
        for (i = 0; i < MAX_ESHOT; i++) { g_eshot[i].vx = 0; g_eshot[i].vy = 0; }   /* 残骸(弾)が一斉に静止する */
    }
}

void ovl_init(void) {
    u8 i;
    g_ter.lo = s2_bot; g_ter.hi = s2_top; g_ter.lo_add = 0; g_ter.ncol = S2_COLS;   /* DROP の爆雷が這う床・天井 */
    for (i = 0; i < 4; i++) { vdp_write_addr17(1, (u16)(0xF800u + (PAT_WELL + i) * 32u)); vdp_out_block(32, well_pat[i]); }
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
    /* ★沈む間は限りを掛けない(地形はまだ画面の下。掛けると自機が一番下へ押し込まれて見えた。M7 の後) */
    if (g_sec <= SEC2_SINK || g_sec == SEC2_OUT) { g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16; return; }
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
    if (g_sec >= SEC2_DOWN && g_sec <= SEC2_CENTER) twinkle();
    switch (g_sec) {
    case SEC2_AIR:
        if (g_sky_t < 16 && !(sec_t & 63)) g_sky_t++;          /* 最後の雲を抜けて、空が藍から黒へ(指示書 5.2) */
        if ((u16)(g_wx - sec_wx) >= AIR_LEN) enter(SEC2_SINK);
        break;
    case SEC2_SINK: {
        u16 d = (u16)(g_wx - sec_wx);
        if (d >= SINK_LEN) { g_scam_q = 0; enter(g_calib ? SEC2_CENTER : SEC2_DOWN); }
        else {
            g_scam_q = (s16)(S_TOP * 16 + (s16)(d * 12u));    /* 192 行 / 256 ドット(× 16) */
        }
        break;
    }
    case SEC2_DOWN:
        if (sec_t == 90) { g_grav = GRAV_DIR; g_gx = 0; g_gy = 1; }
        if (col_of_screen() >= S2_COL_FLIP) enter(SEC2_FLIP);
        break;
    case SEC2_FLIP:
        /* 予告(1.5 秒)の後、1/2 秒かけて画面が縦に一周する。回り終えたら重力は上向き(player.c が強さを立ち上げる) */
        if (sec_t < 90) g_grav = GRAV_NONE;
        else if (sec_t < 122) g_roll = (u8)(g_roll + 8);
        else if (sec_t == 122) { g_roll = 0; g_grav = GRAV_DIR; g_gy = -1; }
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
        if (!g_calib) wells();
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
void ovl_draw(void) {
    if (g_sec == SEC2_CENTER && !g_calib) {                   /* 渦(回る絵。1 枚で行ごとの色) */
        u8 i;
        for (i = 0; i < NWELL; i++) {
            s16 sx = well_sx(i);
            if (sx > -16 && sx < 256) spr_put(sx, well_y[i], (u8)(PAT_WELL + (((sec_t >> 2) + i) & 3)), G_ENEMY, 0);
        }
    }
    b2_draw();
}
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
