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
static u8  regs_top[2][16], regs_n[2][14];
static u8  next_line, roll_row;    /* 次に書く名前、その文字の行 */
static u8  done_line;              /* 画面の上へ抜けて消した名前の数 */
static u16 fill_row;
static u8  const_done;             /* リングの 184..191 行(全列共通のブロック 23)を空にした */               /* スタッフロールの空を塗った行(リングのページ 0、256 で終わり) */
static const u8 black[3] = { 0, 0, 0 };
static const u8 night[3] = { 0, 0, 1 };

static const char *const roll[] = {
    "CRITERION", "", "", "STAFF", "", "GAME DESIGN", "TDF", "", "PROGRAM", "TDF", "", "GRAPHICS", "TDF", "",
    "SOUND", "TDF", "", "", "SPECIAL THANKS", "MSX TURBO R USERS", "", "", "THANK YOU FOR PLAYING", "", "", "",
    "SUBJECT: INFINITAS", "", "STATUS: ~" };   /* ~ は ∞(font.c) */
#define NROLL (sizeof roll / sizeof roll[0])
#define ROLL_S0 (CAM_ROLL0 + 200)  /* 1 行目の s(16 行おき) */

static void enter(u8 sec) { g_sec = sec; sec_t = 0; }

void ovl_init(void) {
    stage_reset_ground();
    g_boss_state = BS_NONE; g_grav = GRAV_NONE; g_hall = 0; g_sky_t = 16;
    enemy_waves(WAVES_NONE);
    yrow = 0; split = 0; next_line = 0; roll_row = 0; done_line = 0; fill_row = 0; const_done = 0;
    g_blk_base = 0; g_blk_skip = 0;                           /* リングの 64..119 行(地球)はカメラが着いてから外す */
    stage_sky_redraw();                                       /* 8 面の縦穴の壁を星空に */
    enter(SECE_SPACE);
}

void ovl_update(void) {
    sec_t++;
    g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16;
    switch (g_sec) {
    case SECE_SPACE:                                          /* 宇宙へ: カメラを地球の帯の高さへ */
        if (g_scam_q > CAM_EARTH * 16) g_scam_q -= 16;
        else {
            /* ★地球の行(リングの 64..119 行)を描き替えから外すのは、この高さに着いてから(終幕に入ってすぐ外していた間は、
               上がっていく途中でその行が画面を通り、夜空に黒い帯が出た) */
            g_blk_base = YJK_ROW / 8; g_blk_skip = YJK_H / 8;
            if (yrow >= YJK_H * 2) { g_bands_all = 1; enter(SECE_EARTH); }
        }
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
        /* ★スタッフロールの間はリングをこの場面で預かる: 空のブロックの描き替えを全部外し(g_blk_skip)、暗いうちに空の色で塗って
           星を打ち、名前は画面に入る前に書いて、上へ抜けたら消す。描き替えと取り合っていた間は、書いた名前が後から来た描き替えに
           消され(前半の名前が出なかった)、リングが 1 周して前の名前が下からまた出た(遊んでの指摘「スタッフロールが壊れている」) */
        if (sec_t == 1) { u8 i; for (i = 2; i < 16; i++) vdp_set_pal(i, 0, 0, 0); g_scam_q = CAM_ROLL0 * 16; g_blk_base = 0; g_blk_skip = 32; g_bands_all = 1; }
        if (sec_t == 49 && fill_row < 256) sec_t = 48;        /* 空を塗り終わるまで暗いまま */
        if (sec_t == 50) { u8 i; for (i = 2; i < 9; i++) vdp_set_pal(i, 1, 1, 2); vdp_set_pal(9, 2, 3, 5); vdp_set_pal(14, 4, 4, 5); vdp_set_pal(15, 7, 7, 7); vdp_set_pal(10, 2, 6, 7); vdp_set_pal(11, 6, 7, 7); vdp_set_pal(12, 6, 5, 2); vdp_set_pal(13, 6, 3, 2); }
        if (sec_t > 50 && (sec_t & 1) && g_scam_q < CAM_ROLL1 * 16) g_scam_q += 16;
        if (g_scam_q >= CAM_ROLL1 * 16) { infx_start(128, 120); enter(SECE_FINAL); }
        break;
    case SECE_FINAL:                                          /* ∞ → 2 周目 */
        if (sec_t == 240) { u8 i; for (i = 0; i < 16; i++) vdp_set_pal(i, 0, 0, 0); }
        if (sec_t == 250) {                                   /* 2 周目の出撃の場面へ(指示書 4.5)。タイトルの素材を読み直し(本体が表示を
                                                                 止めて行う)、ロゴが飛び去る出撃から。得点・残機・強化はそのまま */
            g_loop++;
            g_split_n = 0; g_blk_skip = 0; g_blk_base = 0;
            bg_mix(15);                                       /* ★bg_mix は値が替わったときしか書かないので、一度ずらす(終幕で色 2..7 を
                                                                 黒にしたまま戻らず、2 周目の雲が黒かった) */
            g_after_over = AO_LOOP; g_reload = 1;
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
    /* ★ブロック 23(リングの 184..191 行)は地面の短冊の共通の行で、空の描き替えから外れている。8 面の地面の絵が残り、宇宙へ
       上がると紫の筋に見えたので、終幕に入ったら空の色で塗る(2 周目は地面の列が描き直す) */
    if (!const_done) { vdp_cmd_wait(); vdp_hmmv(0, 184, 256, 8, 0x11); vdp_cmd_wait(); vdp_hmmv(0, 256 + 184, 256, 8, 0x11); const_done = 1; return; }
    if (g_sec == SECE_SPACE && yrow < YJK_H * 2 && g_scam == CAM_EARTH && (u16)(sys_timer() - g_frame_t0) < 150u * 16u) {
        u8 n;
        for (n = 0; n < 2 && yrow < YJK_H * 2; n++, yrow++) {   /* 偶数バイト → ページ 0、奇数 → ページ 2 */
            u8 r = (u8)(yrow < YJK_H ? yrow : yrow - YJK_H);
            if (yrow < YJK_H) rom_to_vram(ASSET_TITLE_EVEN_BANK, (u16)(ASSET_TITLE_EVEN_OFF + (YJK_SRC0 + r) * 128u), 0, (u16)((YJK_ROW + r) * 128u), 128);
            else rom_to_vram(ASSET_TITLE_ODD_BANK, (u16)(ASSET_TITLE_ODD_OFF + (YJK_SRC0 + r) * 128u), 1, (u16)((YJK_ROW + r) * 128u), 128);
        }
    }
    if (g_sec == SECE_ROLL && sec_t > 50 && next_line < NROLL) {   /* 画面の下に入る前の名前を書く */
        s16 s = (s16)(ROLL_S0 + next_line * 16);
        if (s < g_scam + PLAY_H + 12) {               /* ★1 フレームに文字の 1 行ずつ(8 フレームで 1 つ。ロールは 2 フレームに 1 ライン)。
                                                         まとめて書くと空き時間が平常の 8% まで減った(M7) */
            const char *str = roll[next_line];
            u8 n = 0;
            while (str[n]) n++;
            if (n) { vdp_cmd_wait(); font_text_row(0, (u8)(128 - n * 4), (u8)(s + 188), (u8)(next_line >= NROLL - 3 ? 11 : 15), 1, str, roll_row); }
            if (!n || ++roll_row == 8) { roll_row = 0; next_line++; }
        }
    }
    if (g_sec == SECE_ROLL && fill_row < 256) {               /* 空を塗る(1 フレームに 16 行)と星 */
        u8 r = (u8)fill_row, k;
        vdp_cmd_wait(); vdp_hmmv(0, r, 256, 16, 0x11); vdp_cmd_wait();
        for (k = 0; k < 6; k++) {
            u8 x = rnd8(), y = (u8)(r + (rnd8() & 15)), c = (u8)((rnd8() & 3) == 0 ? 15 : (rnd8() & 1) ? 14 : 9);
            vdp_write_addr17(0, (u16)((u16)y * 128u + (x >> 1)));
            VDP_DAT = (x & 1) ? (u8)(0x10 | c) : (u8)((c << 4) | 1);
        }
        fill_row += 16;
    }
    /* 画面の上へ抜けた名前の行は消す(リングは 256 行で回るので、残すと下から同じ名前がまた出た。2 周目の空にも残った) */
    if (g_sec >= SECE_ROLL && done_line < next_line && (s16)(ROLL_S0 + done_line * 16 + 8) <= g_scam) {
        if (roll[done_line][0]) { vdp_cmd_wait(); vdp_hmmv(0, (u8)(ROLL_S0 + done_line * 16 + 188), 256, 8, 0x11); }
        done_line++;
    }
    /* 終幕の最後(画面が黒い間): まだ残っている名前の行を空の色で消す(2 周目の空は名前の行を描き直さない) */
    if (g_sec == SECE_FINAL && sec_t > 240) {
        u8 k = 0;
        while (done_line < next_line && k < 3) {
            if (roll[done_line][0]) { vdp_cmd_wait(); vdp_hmmv(0, (u8)(ROLL_S0 + done_line * 16 + 188), 256, 8, 0x11); k++; }
            done_line++;
        }
    }
}

void ovl_bands(s16 sc, u16 nx) {
    u8 bi = ras_back_index(), n = split;
    RasEntry *e;
    (void)sc;
    /* ★スタッフロールの間は空を 1 色、横位置 0 に(高い空の星の層は帯ごとに横へずれ、ページ 0 に書いた名前が画面の外へ出て
       前半が見えず、塗っていないページ 1 の前の絵が棒になって見えた) */
    if (g_sec >= SECE_ROLL) { stage_ev_pal(0, night); stage_ev_scroll(0, 0); return; }
    /* ★宇宙へ抜ける間(g_bands_all = 0)は高い空の帯の後ろから呼ばれるので、0 行の項目を足さない(前の帯に混ざるか 6 行下へ
       ずれて差し込まれ、夜空の途中に黒い帯が段になって出た。遊んでの指摘「最終ステージクリア後のあれはゴミにしか見えない」) */
    if (!g_bands_all) return;
    stage_ev_pal(0, black);
    stage_ev_scroll(0, nx);
    if (!n) return;
    {
        u8 *rt = regs_top[bi], *rn = regs_n[bi];
        rt[0] = 0; rt[1] = 0x1E; rt[2] = 25; rt[3] = 0x0A; rt[4] = 2; rt[5] = 0x1F;
        rt[6] = 8; rt[7] = 0x2A; rt[8] = 23; rt[9] = YJK_ROW; rt[10] = 5; rt[11] = spr_regs.r5a; rt[12] = 11; rt[13] = 3; rt[14] = 18; rt[15] = R18_G7;
        /* ★地球の帯へ切り替えるのは下の枠の 236 行(244 では R#19 = 240 + 128 = 112 になり、帯の終わり(R#19 = n - 4 + 64)と値が
           6 未満に近づくと割込みが「続けて適用」して 3 行で帯を閉じ、帯が 52 行より伸びると地球が消えた) */
        e = stage_ev_raw(236, RAS_REGS | RAS_SCROLL);
        if (e) { e->haze = rt; e->haze_n = 8; e->r26 = (u8)(VDP_R26(sec_t >> 3) & 31); e->r27 = VDP_R27(sec_t >> 3); }
        rn[0] = 0; rn[1] = 0x16; rn[2] = 25; rn[3] = 0x03; rn[4] = 8; rn[5] = 0x28;
        rn[6] = 2; rn[7] = 0x3F; rn[8] = 23; rn[9] = (u8)(g_vscroll + g_roll); rn[10] = 5; rn[11] = spr_regs.r5a; rn[12] = 18; rn[13] = R18_SAVE;
        e = stage_ev_raw(n, RAS_REGS | RAS_SCROLL | RAS_PAL | RAS_MARK);
        if (e) { e->haze = rn; e->haze_n = 7; e->r26 = VDP_R26(nx); e->r27 = VDP_R27(nx); e->pidx = 1; e->prb = 0; e->pg = 0; }
    }
}
