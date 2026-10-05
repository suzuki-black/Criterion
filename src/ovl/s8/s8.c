/* s8.c — 8 面「原器」の前半のオーバレイ: 空中戦・縦穴・歪曲回廊・中ボス TOLERANCE(指示書 5.8)。
   ・空中戦: 反転した測定空間(黒い方眼)を、全種の編隊の中を上昇しながら飛ぶ(カメラ s = -192 → -560)。
   ・縦穴: 横スクロールを止め(画面をリングのページ 0 にそろえ)、縦スクロールだけで下へ降りる。左右の壁は高い空の
     ブロックに描く(stage.c の g_hall = 2)。降りきると回廊(短冊)が見え、横スクロールに戻る。
   ・歪曲回廊: 帯 10 本の R#26/27 を正弦でずらし、R#23 も少しだけ揺らす(画面全体がうねる)。
     ★R#23 の揺れはスプライトもずらすので ±2 ドットまで。
   ・TOLERANCE: 上下の限界線(BG のバー)が迫って動ける幅を狭める。バーの縁(目盛り)はページ 2 の絵を写し、
     伸びた分は塗る。バーの砲台(スプライト)を壊すとその分だけ押し戻せる。8 基とも壊すとバーが引く。
   → 後半(s8b: 中枢・PROTOTYPE ZERO・脱出)を頼む。地面(短冊と地図)はそのまま後半へ渡す。
   ★読み込まれるのは 7 面のあと(反転した測定空間のまま)。 */
#include "s8.h"
#include "ovl.h"
#include "vdp.h"
#include "rom.h"
#include "ghost.h"
#include "spr.h"
#include "sprite_data.h"
#include "assets.h"
#include "s8_data.h"

#define AIR_T 900

static u16 sec_t;
static u8  amp;                    /* 歪曲の振れ幅: 正弦(±127)を右へずらす数(0 = なし、4 = ±8 ドット)。掛け算を使わない */
static u8  sheet_ld;
static const u8 sky_dark[3] = { 0, 0, 1 }, inside[3] = { 1, 1, 2 };

/* ---- TOLERANCE ---- */
#define TOP0 27                    /* 上のバーの縁の初め(天井の板の下端) */
#define BOT0 160
static s16 top_e, bot_e, top_d, bot_d;   /* 縁の行(画面)/ 描いてある縁 */
static u8  tu_hp[8], mid_t, adv_t;
static const u8 tu_x[4] = { 40, 96, 152, 208 };

static u16 col_of_screen(void) { return (u16)(((u16)(g_wx - (g_w0 << 3))) >> 3); }

static void enter(u8 sec) {
    g_sec = sec; sec_t = 0;
    enemy_waves(sec == SEC8_AIR ? WAVES_AIR : WAVES_NONE);   /* ★歪曲回廊(帯ごとに R#23 を替える、最も重い場面)は編隊なし(M7 の後) */
    if (sec == SEC8_MID) {
        u8 i;
        top_e = top_d = TOP0; bot_e = bot_d = BOT0; mid_t = 0; adv_t = 0;
        for (i = 0; i < 8; i++) tu_hp[i] = 3;
        g_boss_state = BS_FIGHT; g_boss_hp = 24;
    }
}

void ovl_init(void) {
    stage_reset_ground();
    g_w0 = (u16)((g_wx + 1024) >> 3);                         /* 縦穴の底は空中戦の終わりに決め直す */
    rom_to_ram(ASSET_S8_MAP_BANK, ASSET_S8_MAP_OFF, g_map, S8_COLS);
    g_maplen = S8_COLS;
    stage_load_strips(ASSET_S8_STRIPS_BANK, ASSET_S8_STRIPS_OFF);
    stage_set_upper(UP_SKY);
    g_boss_state = BS_NONE; g_grav = GRAV_NONE; g_col_rev = 0; g_hall = 0; g_sky_t = 16;
    amp = 0; sheet_ld = 0;
    enter(SEC8_AIR);
}

static void terrain(void) {
    u16 c0 = (u16)(((u16)(g_wx + (u16)PX(g_pl.x) - (g_w0 << 3))) >> 3), c;
    s16 lo = 0, hi = PLAY_H - 16;
    for (c = c0; c <= (u16)(c0 + 2); c++) {
        u8 k;
        if (c >= S8_COLS) continue;
        k = g_map[c];
        if ((s16)(s8_ceil[k] - g_scam) > lo) lo = (s16)(s8_ceil[k] - g_scam);
        if ((s16)(s8_floor[k] - 16 - g_scam) < hi) hi = (s16)(s8_floor[k] - 16 - g_scam);
    }
    if (g_sec == SEC8_MID) {                                  /* 限界線 */
        if (top_e + 1 > lo) lo = top_e + 1;
        if (bot_e - 17 < hi) hi = bot_e - 17;
        if (hi < lo) { player_hit(); hi = lo; }
    }
    if (hi < lo) hi = lo;
    g_pl_ymin = lo < 0 ? 0 : lo; g_pl_ymax = hi;
}

static u8 shot_rect(s16 x, s16 y, u8 w, u8 h) {
    u8 i, n = 0;
    for (i = 0; i < MAX_PSHOT; i++) {
        Bullet *b = &g_pshot[i];
        s16 bx, by;
        if (!b->on) continue;
        bx = PX(b->x); by = PX(b->y);
        if (bx < x || bx >= x + w || by < y || by >= y + h) continue;
        if (!b->pierce) b->on = 0;
        n++;
    }
    return n;
}

static void mid_update(void) {
    u8 i, left = 0;
    mid_t++;
    g_boss_aim = -1;
    if (g_boss_state == BS_DYING) {                           /* バーが引く */
        if (top_e > TOP0) top_e--;
        if (bot_e < BOT0) bot_e++;
        if (!(mid_t & 7)) { fx_spawn(FX(rnd8()), FX(top_e - 4)); fx_spawn(FX(rnd8()), FX(bot_e + 4)); }
        if (top_e == TOP0 && bot_e == BOT0 && top_d == TOP0 && bot_d == BOT0) g_boss_state = BS_DONE;
        return;
    }
    if (++adv_t >= 12) {                                      /* 迫る(上と下を交互に) */
        adv_t = 0;
        if (bot_e - top_e > 44) { if (mid_t & 16) top_e++; else bot_e--; }
    }
    for (i = 0; i < 8; i++) {
        s16 x = tu_x[i & 3], y = i < 4 ? top_e + 2 : bot_e - 14;
        if (!tu_hp[i]) continue;
        left++;
        if (g_boss_aim < 0) g_boss_aim = (s16)(y + 6);
        if (shot_rect(x - 20, y - 4, 28, 16) && --tu_hp[i] == 0) {   /* 押し戻す。★左へ広げる: 処理落ちのフレームは判定しないので、
                                                                     速い弾が 2 フレームで砲台をすり抜けた(M7) */
            fx_spawn(FX(x), FX(y + 6)); score_add(200);
            if (i < 4) top_e = top_e - 16 < TOP0 ? TOP0 : top_e - 16; else bot_e = bot_e + 16 > BOT0 ? BOT0 : bot_e + 16;
            if (g_boss_hp >= 3) g_boss_hp -= 3;
        }
        if (((mid_t + i * 17) & 63) == 0) enemy_shoot(FX(x), FX(y + 6));
    }
    if (!left) { g_boss_state = BS_DYING; score_add(3000); }
}
static void mid_draw(void) {
    u8 i;
    for (i = 0; i < 8; i++) if (tu_hp[i]) spr_put(tu_x[i & 3] - 8, i < 4 ? top_e + 1 : bot_e - 16, PAT_TURRET, G_ENEMY, 0);
}
/* バー: 縁が動いたら、伸びた所を塗るか引いた所を空に戻し、縁(6 行)を写す(2 つに分ける: ring_cmd の幅は 255 まで) */
static void bar_edge(s16 y) { ring_cmd(0, 512, 0, (s16)(y - 5), 128, 6, 0, 0); ring_cmd(128, 512, 128, (s16)(y - 5), 128, 6, 0, 0); }
static void bar_fill(s16 y, s16 h, u8 c) {
    if (h <= 0) return;
    ring_cmd(0, 0, 0, y, 128, (u8)h, 1, c); ring_cmd(0, 0, 128, y, 128, (u8)h, 1, c);
}
static void mid_vram(void) {
    if (gq_free() < 12) return;
    if (top_e != top_d) {                                     /* 上のバー: 0..top_e 行、縁は top_e - 5..top_e */
        if (top_e > top_d) bar_fill((s16)(top_d - 5), (s16)(top_e - top_d), 0xEE);   /* 前の縁を体で塗る */
        else bar_fill((s16)(top_e + 1), (s16)(top_d - top_e), 0x11);                /* 引いた所を空に */
        bar_edge(top_e);
        top_d = top_e;
        return;
    }
    if (bot_e != bot_d) {                                     /* 下のバー: bot_e..187 行、縁は bot_e..bot_e + 5 */
        if (bot_e < bot_d) bar_fill((s16)(bot_e + 5), (s16)(bot_d - bot_e + 1), 0xEE);
        else bar_fill(bot_d, (s16)(bot_e - bot_d), 0x11);
        bar_edge((s16)(bot_e + 5));
        bot_d = bot_e;
    }
}

void ovl_update(void) {
    sec_t++;
    switch (g_sec) {
    case SEC8_AIR:                                            /* 全種の編隊の中を上昇する */
        if (g_scam_q > S8_SHAFT_TOP * 16) g_scam_q -= 7;
        if (!(sec_t & 63)) enemy_spawn_notch(FX(200 + (rnd8() & 31)), FX(PLAY_H));
        if (sec_t >= AIR_T && g_scam_q <= S8_SHAFT_TOP * 16 && (g_wx & 511u) == 0) {
            g_scroll_speed = 0;
            g_w0 = (u16)(g_wx >> 3);                          /* 画面の左 = 縦穴の底の地図の列 0 */
            g_hall = 2; stage_sky_redraw();
            enter(SEC8_SHAFT);
        }
        break;
    case SEC8_SHAFT:                                          /* 縦スクロールだけで降りる */
        if (g_scam_q < 0) { g_scam_q += 32; if (g_scam_q > 0) g_scam_q = 0; }
        if (!(sec_t & 15)) enemy_bullet(FX(40 + (rnd8() & 127) + (rnd8() & 31)), FX(0), 0, 24);   /* 落ちてくる破片 */
        if (PX(g_pl.x) < 36) g_pl.x = FX(36);
        if (PX(g_pl.x) > 204) g_pl.x = FX(204);
        if (!g_scam_q) { g_hall = 0; g_scroll_speed = 4; enter(SEC8_WARP); }
        break;
    case SEC8_WARP:
        if (amp < 4 && !(sec_t & 31)) amp++;
        if (col_of_screen() + 48 >= S8_COL_MID && amp && !(sec_t & 31)) amp--;
        if (col_of_screen() + 8 >= S8_COL_MID + 8) { amp = 0; g_scroll_speed = 0; enter(SEC8_MID); }
        break;
    case SEC8_MID:
        if (sec_t == 5400) g_boss_state = BS_DONE;
        if (g_boss_state == BS_DONE) {                        /* 後半へ(地面はそのまま) */
            g_boss_state = BS_NONE; g_scroll_speed = 4;
            g_sec = SEC8_CORE;
            ovl_request(OVL_S8B);
            return;
        }
        break;
    default:
        break;
    }
    if (g_sec >= SEC8_WARP) terrain();
    else { g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16; }
}

void ovl_logic(void) { if (g_sec == SEC8_MID && g_boss_state != BS_NONE && g_boss_state != BS_DONE) mid_update(); }
void ovl_draw(void) { if (g_sec == SEC8_MID && g_boss_state == BS_FIGHT) mid_draw(); }
void ovl_vram(void) {
    if (sheet_ld < 6) {                                       /* バーの縁をページ 2 の 0..5 行へ */
        rom_to_vram(ASSET_TBAR_BANK, (u16)(ASSET_TBAR_OFF + sheet_ld * 128u), 1, (u16)(sheet_ld * 128u), 128);
        sheet_ld++;
    }
    if (g_sec == SEC8_MID) mid_vram();
    gq_pump(0);
}

void ovl_bands(s16 sc, u16 nx) {
    u8 k;
    if (g_sec == SEC8_SHAFT || g_sec == SEC8_AIR) return;
    stage_ev_pal((s16)(0 - sc), g_sec >= SEC8_WARP ? inside : sky_dark);
    if (!amp) return;
    /* 歪曲回廊: 16 行ごとの帯の横と縦を正弦でずらす(★12 行ごと 14 本では割込みが重く遅れた) */
    for (k = 1; k < 11; k++) {
        s8 w = g_sin64[(u8)((sec_t >> 1) + k * 8) & 63];
        s16 dx = (s16)(w >> (8 - amp));
        s16 l = (s16)(k * 16 - sc);
        stage_ev_scroll(l, (u16)(nx + dx));
        stage_ev_vscroll(l, (u8)(g_vscroll + g_roll + (w >> 6)));
    }
}
