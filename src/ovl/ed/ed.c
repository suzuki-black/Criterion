/* ed.c — 終幕のオーバレイ(指示書 4.2「エンディング」): 宇宙へ抜ける → 地球を見下ろす SCREEN 12 の自然画帯 →
   スタッフロール(R#23 の縦スクロール)→ SUBJECT INFINITAS / STATUS ∞ → 2 周目の出撃。
   ・地球はタイトルの地平線の YJK(ページ 0 と 2)を使う。カメラ s = -316 のとき見えないリングの 64..119 行に置き、
     列と描き替えから外す(g_blk_base / g_blk_skip)。帯の R#23 = 64 でその行を見せる。
   ・スタッフロール: 世界の横を止め(画面 = リングのページ 0)、カメラを下へ動かし、見えてくる行に名前を
     フォントで書く(高い空のブロックは先回りで描かれるので、書くのは画面に入る直前)。
   ・2 周目: 色を全部黒にしてから雲の空を描き直し(bg_init)、1 面の空中戦から(stage_start)。 */
#include "game.h"
#include "ovl.h"
#include "vdp.h"
#include "rom.h"
#include "font.h"
#include "spr.h"
#include "assets.h"
#include "sys.h"

enum { SECE_SPACE = 65, SECE_EARTH, SECE_ROLL, SECE_FINAL };

#define CAM_EARTH (-316)
#define CAM_ROLL0 (-700)
#define CAM_ROLL1 (-200)
#define YJK_ROW   64
#define YJK_H     56
#define YJK_SRC0  8                /* 地平線の絵(64 行)の 8 行目から(下の端に地平線の光がある) */

static u16 sec_t;
static u8  yrow, split;
static u8  regs_top[2][14], regs_n[2][12];
static u8  next_line;              /* 次に書く名前 */
static const u8 black[3] = { 0, 0, 0 };

static const char *const roll[] = {
    "CRITERION", "", "", "STAFF", "", "GAME DESIGN", "TDF", "", "PROGRAM", "TDF", "", "GRAPHICS", "TDF", "",
    "SOUND", "TDF", "", "", "SPECIAL THANKS", "MSX TURBO R USERS", "", "", "THANK YOU FOR PLAYING", "", "", "",
    "SUBJECT INFINITAS", "", "STATUS INFINITY" };
#define NROLL (sizeof roll / sizeof roll[0])
#define ROLL_S0 (CAM_ROLL0 + 200)  /* 1 行目の s(16 行おき) */

static void enter(u8 sec) { g_sec = sec; sec_t = 0; }

void ovl_init(void) {
    stage_reset_ground();
    g_boss_state = BS_NONE; g_grav = GRAV_NONE; g_hall = 0; g_sky_t = 16;
    enemy_waves(WAVES_NONE);
    yrow = 0; split = 0; next_line = 0;
    g_blk_base = YJK_ROW / 8; g_blk_skip = YJK_H / 8;         /* リングの 64..119 行 = 地球 */
    stage_sky_redraw();                                       /* 8 面の縦穴の壁を星空に */
    enter(SECE_SPACE);
}

void ovl_update(void) {
    sec_t++;
    g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16;
    switch (g_sec) {
    case SECE_SPACE:                                          /* 宇宙へ: カメラを地球の帯の高さへ */
        if (g_scam_q > CAM_EARTH * 16) g_scam_q -= 16;
        else if (yrow >= YJK_H * 2) { g_bands_all = 1; enter(SECE_EARTH); }
        break;
    case SECE_EARTH:                                          /* 地球を見下ろす(帯が伸び、止まり、縮む) */
        if (sec_t < 300) { if (split < YJK_H) split = (u8)(split < 8 ? 8 : split + 2); }
        else if (split) split = (u8)(split > 9 ? split - 2 : 0);
        g_split_n = split;
        if (sec_t > 300 && !split && (g_wx & 511u) == 0) {
            g_bands_all = 0; g_blk_skip = 0; g_scroll_speed = 0;
            enter(SECE_ROLL);
        }
        break;
    case SECE_ROLL:                                           /* スタッフロール: 暗転して高く飛び、下へ流す */
        if (sec_t == 1) { u8 i; for (i = 2; i < 16; i++) vdp_set_pal(i, 0, 0, 0); g_scam_q = CAM_ROLL0 * 16; }
        if (sec_t == 49 && stage_busy()) sec_t = 48;          /* 高く飛んだ先の空が描き終わるまで暗いまま */
        if (sec_t == 50) { u8 i; for (i = 2; i < 9; i++) vdp_set_pal(i, 1, 1, 2); vdp_set_pal(9, 2, 3, 5); vdp_set_pal(14, 4, 4, 5); vdp_set_pal(15, 7, 7, 7); vdp_set_pal(10, 2, 6, 7); vdp_set_pal(11, 6, 7, 7); vdp_set_pal(12, 6, 5, 2); vdp_set_pal(13, 6, 3, 2); }
        if (sec_t > 50 && (sec_t & 1) && g_scam_q < CAM_ROLL1 * 16) g_scam_q += 16;
        if (g_scam_q >= CAM_ROLL1 * 16) { infx_start(128, 120); enter(SECE_FINAL); }
        break;
    case SECE_FINAL:                                          /* ∞ → 2 周目 */
        if (sec_t == 240) { u8 i; for (i = 0; i < 16; i++) vdp_set_pal(i, 0, 0, 0); }
        if (sec_t == 250) {
            g_loop++;
            g_split_n = 0; g_blk_skip = 0; g_blk_base = 0;
            bg_init();                                        /* 雲の空を描き直す(色は bg_init が戻す) */
            g_phase = PH_AIR; g_pt = 0; g_scroll_speed = 4;
            stage_start();                                    /* 1 面の空中戦から(1 面のオーバレイを頼む) */
        }
        break;
    default:
        break;
    }
}

void ovl_logic(void) { }
void ovl_draw(void) { }

void ovl_vram(void) {
    extern u16 g_frame_t0;
    if (g_sec == SECE_SPACE && yrow < YJK_H * 2 && g_scam == CAM_EARTH && (u16)(sys_timer() - g_frame_t0) < 150u * 16u) {
        u8 n;
        for (n = 0; n < 2 && yrow < YJK_H * 2; n++, yrow++) {   /* 偶数バイト → ページ 0、奇数 → ページ 2 */
            u8 r = (u8)(yrow < YJK_H ? yrow : yrow - YJK_H);
            if (yrow < YJK_H) rom_to_vram(ASSET_TITLE_EVEN_BANK, (u16)(ASSET_TITLE_EVEN_OFF + (YJK_SRC0 + r) * 128u), 0, (u16)((YJK_ROW + r) * 128u), 128);
            else rom_to_vram(ASSET_TITLE_ODD_BANK, (u16)(ASSET_TITLE_ODD_OFF + (YJK_SRC0 + r) * 128u), 1, (u16)((YJK_ROW + r) * 128u), 128);
        }
    }
    if (g_sec == SECE_ROLL && sec_t > 50 && next_line < NROLL) {   /* 画面の下に入る直前の名前を書く */
        s16 s = (s16)(ROLL_S0 + next_line * 16);
        if (s < g_scam + PLAY_H + 4) {
            const char *str = roll[next_line];
            u8 n = 0;
            while (str[n]) n++;
            if (n) { vdp_cmd_wait(); font_text(0, (u8)(128 - n * 4), (u8)(s + 188), (u8)(next_line >= NROLL - 3 ? 11 : 15), 1, str); }
            next_line++;
        }
    }
}

void ovl_bands(s16 sc, u16 nx) {
    u8 bi = ras_back_index(), n = split;
    RasEntry *e;
    (void)sc;
    stage_ev_pal(0, black);
    stage_ev_scroll(0, nx);
    if (!n) return;
    {
        u8 *rt = regs_top[bi], *rn = regs_n[bi];
        rt[0] = 0; rt[1] = 0x1E; rt[2] = 25; rt[3] = 0x0A; rt[4] = 2; rt[5] = 0x1F;
        rt[6] = 8; rt[7] = 0x2A; rt[8] = 23; rt[9] = YJK_ROW; rt[10] = 5; rt[11] = spr_regs.r5a; rt[12] = 11; rt[13] = 3;
        e = stage_ev_raw(244, RAS_REGS | RAS_SCROLL);
        if (e) { e->haze = rt; e->haze_n = 7; e->r26 = (u8)(VDP_R26(sec_t >> 3) & 31); e->r27 = VDP_R27(sec_t >> 3); }
        rn[0] = 0; rn[1] = 0x16; rn[2] = 25; rn[3] = 0x03; rn[4] = 8; rn[5] = 0x28;
        rn[6] = 2; rn[7] = 0x3F; rn[8] = 23; rn[9] = (u8)(g_vscroll + g_roll); rn[10] = 5; rn[11] = spr_regs.r5a;
        e = stage_ev_raw(n, RAS_REGS | RAS_SCROLL | RAS_PAL | RAS_MARK);
        if (e) { e->haze = rn; e->haze_n = 6; e->r26 = VDP_R26(nx); e->r27 = VDP_R27(nx); e->pidx = 1; e->prb = 0; e->pg = 0; }
    }
}
