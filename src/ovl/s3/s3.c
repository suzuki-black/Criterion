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

#define AIR_LEN  1200              /* 空中戦の残り(ドット。★4〜6 秒は短すぎた。約 20 秒に。M7 の後) */
#define SINK_LEN 256
#define S_TOP    (-192)

static u16 sec_wx, sec_t;
static u8  bulk_t;
static u8 scan_got;
static s16 scan_x;                 /* Scanner の測定線(停止の間) */
static const u8 inside[3] = { 1, 1, 2 };

/* ---- 隔壁(M7 の後): スプライトの柱で、すき間が 1 ドットずつ滑らかに上下する。★前は地図の列を 1.5 秒ごとに
   書き換えて、すき間がいきなり別の高さへ跳び、予想できなかった。地図の隔壁の列は普通の通路にしてある ---- */
#define PAT_PILLAR 60              /* 柱の絵(面ごとの絵の置き場) */
#define GAP_H      60              /* すき間の高さ(★50 から広げた。高速の区間の指摘) */
static const u8 pillar_pat[32] = {    /* 16x16 の中の 4..11 列: 縁と斜めの縞(色は行ごと) */
    0x0D, 0x0E, 0x0F, 0x0D, 0x0E, 0x0F, 0x0D, 0x0E, 0x0F, 0x0D, 0x0E, 0x0F, 0x0D, 0x0E, 0x0F, 0x0D,
    0xB0, 0x70, 0xF0, 0xB0, 0x70, 0xF0, 0xB0, 0x70, 0xF0, 0xB0, 0x70, 0xF0, 0xB0, 0x70, 0xF0, 0xB0 };
static u8 bulk_ph;                 /* すき間の位相(16 フレームに 1 進む。★4 では動きが速く、見てから向かうと着く頃には
                                      18 ドットずれた) */
static u8 chase, lit;                   /* 流れる照明の位相(色 2..5 のどれが明るいか。1/8 単位) */

/* ★流れる照明(パレットアニメ。M7 の後): 照明は列ごとに色 2..5 を順に使っているので、明るい色を 1 つずつ
   回すと壁を光が流れて見える。速いほど速く流す。CPU は 1 フレームにパレット 4 回だけ */
static void lights(void) {
    u8 i, ph;
    s8 sp = g_scroll_speed;
    chase = (u8)(chase + 3 + (u8)(sp < 0 ? -sp : sp));
    ph = (u8)((chase >> 3) & 3);
    for (i = 0; i < 4; i++) {
        u8 d = (u8)((ph - i) & 3);                            /* 0 = 明るい、1 = 残光、2・3 = 暗い */
        if (d == 0) vdp_set_pal((u8)(2 + i), 7, 7, 6);
        else if (d == 1) vdp_set_pal((u8)(2 + i), 4, 4, 3);
        else vdp_set_pal((u8)(2 + i), 1, 1, 2);
    }
}

static s16 gap_top(u8 i) { return (s16)(94 - GAP_H / 2 + ((g_sin64[(u8)(bulk_ph + i * 8) & 63] * 48) >> 7)); }   /* 通り道 16..171。★隣の隔壁との位相の差は 8(21 では
                                                                 すき間が 80 ドットも跳び、高速では追いつけなかった) */
static s16 bulk_sx(u8 i) { return (s16)((g_w0 + s3_bulk[i]) * 8u - g_wx); }
static u8 bulk_at(u16 c) { u8 i; for (i = 0; i < S3_NBULK; i++) if (s3_bulk[i] == c) return i; return 0xFF; }

static void set_streak(u8 on) {
    u8 i;
    for (i = 6; i < 10; i++) g_strip_xlat[i] = (u8)(on ? i + 4 : i);
    g_strip_xlat[14] = (u8)(on ? 15 : 14);
}

static void enter(u8 sec) {
    g_sec = sec; sec_wx = g_wx; sec_t = 0;
    g_wave_mirror = 0;
    switch (sec) {
    case SEC3_AIR: enemy_waves(g_calib ? WAVES_AIR : WAVES_SKY); break;
    case SEC3_SINK: case SEC3_ACCEL: case SEC3_TOP: enemy_waves(WAVES_AIR); break;
    case SEC3_REV: g_wave_mirror = 1; enemy_waves(WAVES_AIR); break;
    default: enemy_waves(WAVES_NONE); break;
    }
    if (sec == SEC3_REV) g_col_rev = 31;                      /* 逆走: 窓は画面の左へ */
    if (sec == SEC3_MID) { g_col_rev = 16; b3_start(B3_MET); }   /* 振り子で前後: 窓は左右に半分ずつ */
    if (sec == SEC3_TOP) {
        g_col_rev = 0; g_scroll_speed = g_calib ? 4 : 24; set_streak(!g_calib);
        g_map[S3_COL_STOP + 2] = 17; g_map[S3_COL_STOP + 6] = 16;   /* 停止の隔壁は開いている(まだ窓の外) */
        if (!g_calib) b3_load_dial();
    }
    if (sec == SEC3_BOSS) { g_col_rev = 16; b3_start(B3_TACHO); }  /* 突進で前後するので窓は左右に */
    if (sec == SEC3_OUT) { g_col_rev = 0; set_streak(0); }
}

void ovl_init(void) {
    g_ter.lo = s3_ymax; g_ter.hi = s3_ymin; g_ter.lo_add = 16; g_ter.ncol = S3_COLS;   /* DROP の爆雷が這う床・天井 */
    stage_reset_ground();
    g_w0 = (u16)((g_wx + AIR_LEN) >> 3);
    rom_to_ram(ASSET_S3_MAP_BANK, ASSET_S3_MAP_OFF, g_map, S3_COLS);
    g_maplen = S3_COLS;
    stage_load_strips(ASSET_S3_STRIPS_BANK, ASSET_S3_STRIPS_OFF);
    vdp_set_pal(6, 2, 2, 3);                                  /* 壁の影(暗い灰) */
    g_boss_state = BS_NONE; g_grav = GRAV_NONE; bulk_t = 0; bulk_ph = 0; lit = 0;
    { u8 i; for (i = 0; i < S3_NBULK; i++) g_map[s3_bulk[i]] = 14; }   /* 隔壁の列は通路にして、柱はスプライトで */
    vdp_write_addr17(1, (u16)(0xF800u + PAT_PILLAR * 32u)); vdp_out_block(32, pillar_pat);
    { u8 i; vdp_write_addr17(1, (u16)(0xF800u + PAT_SCAN * 32u)); for (i = 0; i < 32; i++) VDP_DAT = i < 16 ? 0x01 : 0x80; }   /* 測定線(縦 2 ドット) */
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
    /* ★沈む間は限りを掛けない(地形はまだ画面の下。掛けると自機が一番下へ押し込まれて見えた。M7 の後) */
    if (g_sec <= SEC3_SINK || g_sec == SEC3_OUT) { g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16; return; }
    for (c = c0; c <= (u16)(c0 + 2); c++) {
        u8 k;
        if (c >= S3_COLS) continue;
        k = g_map[c];
        if (s3_ymin[k] > lo) lo = s3_ymin[k];
        if (s3_ymax[k] < hi) hi = s3_ymax[k];
        {   /* 隔壁の柱: その時のすき間 */
            u8 b = bulk_at(c);
            if (b != 0xFF && (g_sec == SEC3_ACCEL || g_sec == SEC3_TOP)) {
                s16 t = gap_top(b);
                if (t > lo) lo = t;
                if (t + GAP_H - 16 < hi) hi = (s16)(t + GAP_H - 16);
            }
        }
    }
    lo -= g_scam; hi -= g_scam;
    if (hi < lo) hi = lo;                                     /* 閉じた隔壁: 押し戻される(上下には動けない) */
    g_pl_ymin = lo < 0 ? 0 : lo;
    g_pl_ymax = hi > PLAY_H - 16 ? PLAY_H - 16 : hi;
    /* 閉じた隔壁(すき間なし)の列に入ったら、左へ押し戻す */
    if (s3_ymax[g_map[(u16)(c0 + 2) < S3_COLS ? (u16)(c0 + 2) : 0]] == 16 && g_pl.x > FX(8)) g_pl.x -= FX(1);   /* 閉じた隔壁(限り 16..16) */
}

/* 隔壁: すき間の位相を進める */
static void bulkheads(void) {
    if (!(++bulk_t & 15)) bulk_ph++;
}

void ovl_update(void) {
    if (g_sec >= SEC3_ACCEL && g_sec <= SEC3_BOSS) { lights(); lit = 1; }
    else if (lit) { lit = 0; bg_mix(15); bg_mix(16); }      /* 色 2..5 は雲の色。bg_mix は値が替わったときだけ書くので 2 度呼ぶ */
    sec_t++;
    /* ★高速の区間は自機の上下を速くする(スクロール 6 ドット/フレームで 1.5 → 4。隔壁は画面に入ってから 32〜36 フレームで
       来るので、1.5 では反射神経が良くても間に合わなかった。指摘。反応 20 フレームで見てから向かって全部抜けられる
       ことを、すき間の高さ・動きと合わせて計算で確かめた) */
    { s8 sp = g_scroll_speed; g_pl_boost = (g_sec == SEC3_ACCEL || g_sec == SEC3_TOP) && sp >= 8 ? (u8)((sp - 4) << 1) : 0; }
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
        s8 sp = c < 96 ? 2 : c < 144 ? 4 : c < 192 ? 8 : c < 240 ? 16 : 24;   /* 0.5 → 1 → 2 → 4 → 6(★8 は狭い画面で避けきれなかった。M7 の後) */
        if (g_scroll_speed < sp && !(sec_t & 3)) g_scroll_speed++;
        set_streak(g_scroll_speed >= 16);
        bulkheads();
        if (c + 34 >= S3_COL_STOP) enter(SEC3_STOP);
        break;
    }
    case SEC3_STOP:
        /* 1/2 秒で 0 に。静寂の中、Scanner の測定線が左から右へ縦断する */
        if (g_scroll_speed > 0) { g_scroll_speed -= 2; if (g_scroll_speed < 0) g_scroll_speed = 0; set_streak(0); }
        if (sec_t == 40) { scan_x = 0; scan_got = 0; }
        if (sec_t > 40) {
            scan_x += 3;
            /* 当たると強化を 1 段奪う(指示書 3 章。被弾ではない) */
            if (!scan_got && g_pl.alive && PX(g_pl.x) + 8 > scan_x - 3 && PX(g_pl.x) + 8 < scan_x + 3) { scan_got = 1; gauge_steal(); }
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
    if (g_sec == SEC3_OUT && !g_calib && sec_t >= 60) {      /* 宇宙へ出るとガス惑星が見える(4 面の予告) */
        s16 x = (s16)(208 - (sec_t >> 3)), y = (s16)(36 - (g_scam_q >> 4) + S_TOP);
        spr_put(x, y, PAT_GAS_TL, G_MISC, CS_QUANTA); spr_put((s16)(x + 16), y, PAT_GAS_TR, G_MISC, CS_QUANTA);
        spr_put(x, (s16)(y + 16), PAT_GAS_BL, G_MISC, CS_QUANTA); spr_put((s16)(x + 16), (s16)(y + 16), PAT_GAS_BR, G_MISC, CS_QUANTA);
    }
    if (g_sec == SEC3_ACCEL || g_sec == SEC3_TOP) {           /* 隔壁の柱(すき間の上と下にスプライトを積む) */
        u8 i;
        for (i = 0; i < S3_NBULK; i++) {
            s16 sx = bulk_sx(i), t, y;
            if (sx < -12 || sx > 252) continue;
            t = gap_top(i);
            for (y = (s16)(t - 16); y > 16 - 16; y -= 16) spr_put(sx - 4, y < 16 ? 16 : y, PAT_PILLAR, G_ENEMY, 0);
            for (y = (s16)(t + GAP_H); y < 172; y += 16) spr_put(sx - 4, y > 156 ? 156 : y, PAT_PILLAR, G_ENEMY, 0);
        }
    }
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
