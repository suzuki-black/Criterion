/* title.c — 起動ロゴ・タイトル・出撃(指示書 4.2)。一本のフィルムとして、画面を消さずに 1 面の空中戦へつなぐ。

   起動ロゴ(PH_LOGO): 黒地に TDF の紋章(ページ 3 の 140..187 行)。紋章は色 15 だけで描いてあり、ラスターで
     「L 行より上は白、L 行から下は黒」にして L を 1 フレーム 1 行ずつ下げる = 走査線 1 本ずつ描き込まれるように
     出る。出たら色 15 を暗くして消す。
   タイトル(PH_TITLE):
       0..n-1   SCREEN12(夜明けの地球の地平線、ゆっくり横スクロール)。n は 0 → 64 へ 2 行ずつ伸びる
       n..63    黒(何も描いていないページ 3 を見せる)
       64..127  ロゴ「CRITERION」(ページ 2 の 64..127 行)。8 行ずつの 8 帯が左右交互から滑り込んで組み上がる
       128..187 夜の雲海(表示リング)と、その上を飛ぶ Infinitas
       188..211 HUD 帯(ページ 2 の 212..235 行)に「PUSH SPACE KEY」(色 11 を HUD 帯の項目で点滅)
   出撃(PH_SORTIE): SPACE で始まる。自機はこの時点から操作できる。
       ロゴの帯が左右へ飛び去り、地平線の帯は上へ縮む(縮んだ分の行を空の色で描き戻し、表示リングを見せる)。
       ロゴの地は色 1(帯ごとにラスターで空の色)で描いてあるので、表示リングに切り替えても色が跳ばない。
       ★色 0 を空の色へ寄せる案は、外枠(R#7 = 色 0)まで染まったのでやめた。
       空と雲の色は夜から夕焼けへ移り、スクロールは加速してから 1 ドット/フレームに落ち着き、PH_AIR へ。
   ★上の帯が SCREEN12 の間は VRAM を下の区間でしか書けない(M0-4)。n 行の項目に RAS_MARK を付け、メインは
     それを待ってから書く(main.c)。上の帯への切り替えは 244 行(R#19 = 240)で行う。
   ★「ロゴの下に回転する∞の紋章」(ROTOR)と、カタパルトからの射出・オーケストラヒット(PCM)は M3 / M7。 */
#include "game.h"
#include "vdp.h"
#include "raster.h"
#include "spr.h"
#include "rom.h"
#include "font.h"
#include "input.h"
#include "assets.h"
#include "sprite_data.h"
#include "snd.h"
#include "snd_data.h"

#define LOGO_Y     64
#define LOGO_BANDS 8
#define TDF_ROW    140            /* 紋章のページ 3 の行 */
#define TDF_LINE   82             /* 紋章を出す画面の行 */
#define TOP_SWITCH 244            /* 上の帯へ切り替える行 */
#define YJK_H      64             /* 地平線の帯の最大行数(ロゴのすぐ上まで)。★n 行の項目はロゴの 64 行の項目と RAS_GAP 未満に
                                     近づくので、n が 58 以上のときは 64 行の項目にまとめる(title_build)。56 で止めていた間は
                                     56..63 行に暗い帯が残り、走査線の処理に失敗したように見えた */

#define R0_G4   0x16              /* GRAPHIC4 + IE1 */
#define R0_G7   0x1E              /* GRAPHIC7 + IE1 */
#define R25_G4  0x03              /* SP2 | MSK */
#define R25_YJK 0x0A              /* YJK | MSK */
#define R8_SPR  0x28
#define R8_NOSP 0x2A


static s16 band_x[LOGO_BANDS];    /* ロゴの帯の横位置(0 = 組み上がり) */
static u8  wiped;                 /* 出撃: 空の色で描き戻した行の上端 */
static u8  ring_logo;             /* 1 = ロゴの区間も表示リングにした */
static u8  fade15;                /* 紋章の明るさ(0..7) */
static u8  reveal;                /* 紋章を見せる行 */
static u16 yjk_q;                 /* 地平線の横位置(1/8 ドット) */

static u8 regs_top[2][16], regs_n[2][12], regs_ring[2][4], regs_b[2][4];

/* ---- 隠しコンフィグ: タイトルで 上 上 下 下 左 右 左 右 B A(B と A はキーボードの B・A キー) ----
   HUD の帯の 2 行に出す。上下で項目、左右で値、SPACE で決定。★オーバレイの変数は初期化されないので title_start で */
enum { CF_INV, CF_STAGE, CF_BGM, CF_EXIT, CF_N };
static u8 cf_on, cf_k, cf_item, cf_bgm, cf_dirty, cf_keys, cf_row, cf_cur;   /* cf_cur: 書いている行(bit) */
static const u8 cf_seq[10] = { IN_UP, IN_UP, IN_DOWN, IN_DOWN, IN_LEFT, IN_RIGHT, IN_LEFT, IN_RIGHT, 0x80, 0x40 };   /* 0x80 = B、0x40 = A */
static const char *const cf_song[SND_NSONG + 1] = {
    "M01", "M02", "M03", "M04", "M05", "M06", "M07", "M08", "M09", "M10", "M11", "M12", "M13", "M14A", "M14B", "M14C",
    "M15", "M16", "M17", "J01", "J02", "STOP" };
static char cf_line[20];
#define CF_COPY "@ 2026 SUZUKI-BLACK"   /* 著作権表示(@ は ©。font.c)。19 文字 = x 52..203 */

__sfr __at(0xA9) CF_PPI_B;
__sfr __at(0xAA) CF_PPI_C;

/* キーボードの行 2 の B(bit7)と A(bit6)。押されているビットが 1 */
static u8 cf_ba(void) {
    u8 v;
    __asm di __endasm;
    CF_PPI_C = (u8)((CF_PPI_C & 0xF0) | 2);
    v = CF_PPI_B;
    __asm ei __endasm;
    return (u8)(~v & 0xC0);
}

static void cf_put(const char *s, u8 at) { while (*s && at < 19) cf_line[at++] = *s++; }

/* 1 行目の文字列を組む(18 文字、空白で埋める。元のタイトルの文字と同じ長さ) */
static void cf_build(void) {
    u8 i;
    for (i = 0; i < 19; i++) cf_line[i] = ' ';
    cf_line[19] = 0;
    if (cf_item == CF_INV) { cf_put("INVINCIBLE", 1); cf_put(g_dbg_invul ? "ON" : "OFF", 14); }
    else if (cf_item == CF_STAGE) { cf_put("STAGE", 1); cf_line[14] = (char)('0' + (g_start_stage ? g_start_stage : 1)); }
    else if (cf_item == CF_BGM) { cf_put("BGM", 1); cf_put(cf_song[cf_bgm], 14); }
    else cf_put("EXIT", 1);
}

/* タイトルの間、毎フレーム: コマンドを数え、入ったらコンフィグを動かす。1 = コンフィグ中(出撃しない) */
static u8 cf_update(void) {
    u8 ba = cf_ba(), ba_edge = (u8)(ba & ~cf_keys), e = (u8)((g_in_edge & 0x0F) | ba_edge);
    cf_keys = ba;
    if (!cf_on) {
        if (e) {
            if (e == cf_seq[cf_k]) { if (++cf_k == 10) { cf_on = 1; cf_item = 0; cf_dirty = 3; cf_k = 0; } }
            else cf_k = (u8)(e == cf_seq[0] ? 1 : 0);
        }
        return cf_on;
    }
    if (g_in_edge & IN_UP) { cf_item = (u8)(cf_item ? cf_item - 1 : CF_N - 1); cf_dirty |= 1; if (cf_cur == 1) cf_dirty |= 0x10; }
    if (g_in_edge & IN_DOWN) { cf_item = (u8)(cf_item + 1 < CF_N ? cf_item + 1 : 0); cf_dirty |= 1; if (cf_cur == 1) cf_dirty |= 0x10; }
    if (g_in_edge & (IN_LEFT | IN_RIGHT)) {
        u8 r = (u8)(g_in_edge & IN_RIGHT);
        if (cf_item == CF_INV) g_dbg_invul = (u8)!g_dbg_invul;
        else if (cf_item == CF_STAGE) {
            u8 s = g_start_stage ? g_start_stage : 1;
            s = r ? (u8)(s < 8 ? s + 1 : 1) : (u8)(s > 1 ? s - 1 : 8);
            g_start_stage = s;
        } else if (cf_item == CF_BGM) cf_bgm = r ? (u8)(cf_bgm < SND_NSONG ? cf_bgm + 1 : 0) : (u8)(cf_bgm ? cf_bgm - 1 : SND_NSONG);
        cf_dirty |= 1; if (cf_cur == 1) cf_dirty |= 0x10;
    }
    if (g_in_edge & IN_A) {
        if (cf_item == CF_BGM) bgm_play(cf_bgm < SND_NSONG ? cf_bgm : SND_NONE);
        else if (cf_item == CF_EXIT) { cf_on = 0; cf_dirty = (u8)((cf_dirty & 0x10) | 0x0C); bgm_play(SONG_M01); }   /* タイトルの文字を戻す */
    }
    return 1;
}

/* ---- 素材 ---- */
void title_load(void) {
    vdp_hmmv(0, 768, 256, 128, 0x11);                        /* ページ 3 の 0..127 行: 空の色(色 1)。ロゴが滑り込む間に見える */
    vdp_hmmv(0, 768 + 128, 256, 60, 0x00);                   /* ページ 3 の 128..187 行: 黒(紋章の地)。
                                                                ★192 行から先はスプライトの表(0x1E000〜)。211 行まで
                                                                  消していて、上の帯の色表が 0 になり敵が黒く点滅した */
    vdp_cmd_wait();
    rom_to_vram(ASSET_TITLE_EVEN_BANK, ASSET_TITLE_EVEN_OFF, 0, 0x0000, ASSET_TITLE_EVEN_LEN);   /* ページ 0 の 0..63 行 */
    rom_to_vram(ASSET_TITLE_ODD_BANK, ASSET_TITLE_ODD_OFF, 1, 0x0000, ASSET_TITLE_ODD_LEN);     /* ページ 2 の 0..63 行 */
    rom_to_vram(ASSET_LOGO_BANK, ASSET_LOGO_OFF, 1, (u16)LOGO_Y * 128u, ASSET_LOGO_LEN);        /* ページ 2 の 64..127 行 */
    rom_to_vram(ASSET_TDF_BANK, ASSET_TDF_OFF, 1, (u16)(0x8000u + TDF_ROW * 128u), ASSET_TDF_LEN); /* ページ 3 */
    vdp_hmmv(0, 512 + 128, 256, 84, 0x00);                   /* ページ 2 の 128..211 行: 起動ロゴの黒(ゲーム中は
                                                                PLUMB の絵や面の開始表示に使うので描き直す) */
    vdp_hmmv(0, 512 + HUD_VROW, 256, 24, 0x00);               /* HUD 帯は空にしておく(文字はタイトルに入ってから) */
    vdp_cmd_wait();
}

static void enter(u8 ph) {
    u8 i;
    u16 pt_prev = g_pt;
    g_phase = ph;
    g_pt = 0;
    (void)pt_prev;
    if (ph != PH_SORTIE) g_hud_on = 0;
    if (ph == PH_LOGO) {
        reveal = TDF_LINE; fade15 = 7; g_split_n = 0; g_split_shown = 0;
        vdp_set_pal(1, 0, 0, 0);                              /* 起動ロゴの間は空の色(色 1)も黒 */
    }
    if (ph == PH_TITLE) {
        for (i = 0; i < LOGO_BANDS; i++) band_x[i] = (i & 1) ? -256 : 256;   /* 奇数帯は左から、偶数帯は右から */
        g_split_n = 0; ring_logo = 0;
        g_scroll_speed = 1;
        bg_mix(0);
        vdp_set_pal(15, 7, 7, 7);
        infx_title(1);                                        /* ロゴの下で ∞ が回る(ROTOR) */
    }
    if (ph == PH_SORTIE) {
        s16 y = (s16)(150 + (g_sin64[(u8)(pt_prev >> 1) & 63] >> 4));   /* タイトルで最後に描いた位置 */
        wiped = 64;                                       /* ページ 0 の 0..63 行に地平線の偶数バイトが入っている */
        game_start();
        g_pl.x = FX(40); g_pl.y = FX(y); g_pl.invul = 0;      /* タイトルで飛んでいた位置からそのまま操作 */
    }
}

/* 寄せる: 現在値を目標へ 1/4 ずつ(最低 1) */
static s16 ease(s16 v, s16 target) {
    s16 d = (s16)(target - v);
    s16 step = (s16)(d / 4);
    if (!step) step = d > 0 ? 1 : d < 0 ? -1 : 0;
    return (s16)(v + step);
}

void title_update(void) {
    u8 i;
    if (g_phase == PH_LOGO) {
        if (g_pt < 60) { if (reveal < TDF_LINE + 48) reveal++; }
        else if (g_pt >= 110 && fade15 && !(g_pt & 3)) fade15--;
        if (g_pt >= 150) enter(PH_TITLE);
        return;
    }
    if (g_phase == PH_TITLE) {
        if (g_split_n < YJK_H && g_pt >= 4) g_split_n = (u8)(g_split_n < 8 ? 8 : g_split_n + 2);   /* 地平線の帯が上から伸びる */
        for (i = 0; i < LOGO_BANDS; i++)
            if (g_pt >= 24 + i * 6) band_x[i] = ease(band_x[i], 0);
        yjk_q++;
        if (cf_update()) return;                              /* 隠しコンフィグ */
        if ((g_in_edge & IN_A) && g_pt >= 30) { g_demo = 0; enter(PH_SORTIE); }
        else if (g_pt >= 900 && !cf_on && !g_in) { g_demo = 1; enter(PH_SORTIE); }   /* ★15 秒触らなければデモ(M7 の後) */
        return;
    }
    if (g_phase == PH_SORTIE) {
        /* ロゴの帯が飛び去る */
        for (i = 0; i < LOGO_BANDS; i++)
            if (g_pt >= i * 3) band_x[i] = ease(band_x[i], (i & 1) ? 256 : -256);
        /* 地平線の帯が縮む(2 行 / フレーム)。8 行未満になったら分割をやめる */
        if (g_pt >= 10 && g_split_n) g_split_n = (u8)(g_split_n > 9 ? g_split_n - 2 : 0);
        /* ロゴが飛び去ったら表示リングへ(ロゴの地は色 1 = 空の色なので跳ばない) */
        if (g_pt == 52) ring_logo = 1;
        /* 夜 → 夕焼け、スクロールの加速と落ち着き */
        if (g_pt >= 30 && !(g_pt & 3)) bg_mix((u8)((g_pt - 30) >> 2));
        if (g_pt < 60) g_scroll_speed = (u8)(1 + g_pt / 8);
        else if (g_pt < 120 && !(g_pt & 7) && g_scroll_speed > 4) g_scroll_speed--;
        if (g_pt >= 120 && !g_split_n && ring_logo) { g_phase = PH_AIR; g_pt = 0; g_scroll_speed = 4; bg_mix(16); stage_start(); }
    }
}

void title_draw(void) {
    if (g_phase == PH_TITLE) {
        s16 y = (s16)(150 + (g_sin64[(u8)(g_pt >> 1) & 63] >> 4));
        spr_put(40, y, PAT_SHIP_A, G_SHIP_A, 0);
        spr_put(40, y, PAT_SHIP_B, G_SHIP_B, 0);
    }
}

/* 下の区間でする VRAM の仕事(main.c が RAS_MARK を待ってから呼ぶ) */
void title_vram(void) {
    /* タイトルの文字(この時点ではまだ分割していない)。1 フレームに 1 行(まとめるとフレームが落ちた) */
    if (g_phase == PH_TITLE && g_pt == 1) font_text(HUD_PAGE, 72, HUD_ROW1, 11, 0, "PUSH SPACE KEY");
    if (g_phase == PH_TITLE && g_pt == 2) font_text(HUD_PAGE, 52, HUD_ROW2, 14, 0, CF_COPY);
    if (g_phase == PH_TITLE && cf_dirty) {                    /* 隠しコンフィグ。★1 フレームに文字の 1 ドット行ずつ(72 バイト、約 0.6ms)。
                                                                 1 行(576 バイト、約 5ms)をまとめて書くと終わりが上の帯(SCREEN 12)の
                                                                 表示に入り、書いた所が化けた */
        const char *t;
        u8 row, fg;
        if (!cf_row) {                                        /* 書く行は書き始めに決める */
            cf_cur = (u8)((cf_dirty & 1) ? 1 : (cf_dirty & 2) ? 2 : (cf_dirty & 4) ? 4 : 8);
            cf_dirty &= (u8)~0x10;
            if (cf_cur == 1) cf_build();
        }
        if (cf_cur == 1) { t = cf_line; row = HUD_ROW1; fg = 15; }   /* 白(色 11 は点滅に使う) */
        else if (cf_cur == 2) { t = " UD ITEM LR CHANGE "; row = HUD_ROW2; fg = 14; }
        else if (cf_cur == 4) { t = "  PUSH SPACE KEY   "; row = HUD_ROW1; fg = 11; }
        else { t = CF_COPY; row = HUD_ROW2; fg = 14; }
        font_text_row(HUD_PAGE, 52, row, fg, 0, t, cf_row);    /* 元の 2 行目と同じ x = 52..203、19 文字(元の 1 行目 72..183 も覆う) */
        if (++cf_row == 8) {
            cf_row = 0;
            if (!(cf_cur == 1 && (cf_dirty & 0x10))) cf_dirty &= (u8)~cf_cur;   /* 途中で値が変わったら書き直す */
            cf_cur = 0;
        }
    }
    if (g_phase == PH_SORTIE) {
        if (g_pt == 1) vdp_hmmv(0, 512 + HUD_VROW, 256, 24, 0x00);   /* タイトルの文字を消す */
        /* ゲームの HUD は上の帯(SCREEN12)が消えてから描く。★SCREEN12 の表示中に VRAM の書き込みが 244 行を
           越えると、書き込みが別の場所へ化ける(HUD の文字が崩れ、プレイエリアに黒い点が出た) */
        if (!g_hud_on && !g_split_n && !g_split_shown) { hud_init(); g_hud_on = 1; }   /* 表示中のリストも分割なし */
        /* 地平線の帯が縮んだ分を空の色で描き戻す(ページ 0 の 0..63 行は地平線の偶数バイトが入っている) */
        if (g_split_n < wiped) {
            u8 top = g_split_n;
            vdp_hmmv(0, top, 256, (u16)(wiped - top), 0x11);
            wiped = top;
        }
    }
}

/* ---- ラスターリスト ---- */
static RasEntry *put_e(RasEntry *e, u8 line, u8 flags) { e->line = line; e->flags = flags; return e; }

static void sky_pal(RasEntry *e, u8 band) {
    const u8 *c = bg_sky(band);
    e->pidx = 1; e->prb = (u8)((c[0] << 4) | c[2]); e->pg = c[1];
}

void title_build(void) {
    RasEntry *e = ras_back();
    u8 bi = ras_back_index(), k = 0, i;
    u8 *rt = regs_top[bi], *rn = regs_n[bi], *rr = regs_ring[bi], *rb = regs_b[bi];
    u8 n = g_split_n, merge;

    if (g_phase == PH_LOGO) {
        /* 紋章: ページ 3 を見せる(横 256 = ページ 3 の左端、R#23 で紋章の行を TDF_LINE に) */
        rt[0] = 2; rt[1] = 0x7F; rt[2] = 23; rt[3] = (u8)(TDF_ROW - TDF_LINE);
        put_e(&e[k], 0, RAS_REGS | RAS_SCROLL | RAS_PAL);
        e[k].haze = rt; e[k].haze_n = 2; e[k].r26 = VDP_R26(256u); e[k].r27 = VDP_R27(256u);
        e[k].pidx = 15; e[k].prb = (u8)((fade15 << 4) | fade15); e[k].pg = fade15; k++;
        if (reveal < TDF_LINE + 48) {
            put_e(&e[k], reveal, RAS_PAL); e[k].pidx = 15; e[k].prb = 0; e[k].pg = 0; k++;
        }
        /* 紋章より下: ページ 2 の 128..211 行(何も描いていない黒)を見せる。
           ★R#23 = 58 のままだとページ 3 の 188 行以降(スプライトの表)が見える。また HUD 帯の項目は横位置 0 を
             先に書くので、その前の数行が R#23 の古い値のままページ 2 になる(地平線の奇数バイトが 1 行見えた) */
        rn[0] = 23; rn[1] = 8;                                 /* 130..187 行 → 138..195 行(数行早く効いても黒の中) */
        put_e(&e[k], (u8)(TDF_LINE + 48), RAS_REGS | RAS_SCROLL); e[k].haze = rn; e[k].haze_n = 1;
        e[k].r26 = 0; e[k].r27 = 0; k++;
        bg_fill_hud(&e[k], bi);
        e[k].flags |= RAS_PAL; e[k].pidx = 11; e[k].prb = 0; e[k].pg = 0;   /* HUD 帯の文字(色 11)はまだ見せない */
        k++;
        ras_commit(k);
        g_split_shown = 0;
        return;
    }

    /* 上の帯(SCREEN12)は 244 行で切り替える */
    if (n) {
        u16 yx = yjk_q >> 3;
        rt[0] = 0; rt[1] = R0_G7; rt[2] = 25; rt[3] = R25_YJK; rt[4] = 2; rt[5] = 0x1F;
        rt[6] = 8; rt[7] = R8_NOSP; rt[8] = 23; rt[9] = 0; rt[10] = 5; rt[11] = spr_regs.r5a; rt[12] = 11; rt[13] = 3;
        put_e(&e[k], TOP_SWITCH, RAS_REGS | RAS_SCROLL);
        e[k].haze = rt; e[k].haze_n = 7; e[k].r26 = (u8)(VDP_R26(yx) & 31); e[k].r27 = VDP_R27(yx); k++;   /* SP2 なし = 256 幅 */
    }
    /* n 行: SCREEN5 へ。タイトル中はページ 3(黒)、出撃中は表示リング(描き戻した空)。
       ★ロゴの 64 行に近い(RAS_GAP 未満)ときは、ロゴの最初の帯の項目にまとめる(merge) */
    merge = n >= LOGO_Y - RAS_GAP;
    {
        u8 ring = g_phase == PH_SORTIE && !(merge && !ring_logo);
        rn[0] = 0; rn[1] = R0_G4; rn[2] = 25; rn[3] = R25_G4; rn[4] = 8; rn[5] = R8_SPR;
        rn[6] = 2; rn[7] = ring ? 0x3F : 0x7F; rn[8] = 23; rn[9] = g_vscroll;
        rn[10] = 5; rn[11] = spr_regs.r5a;
        if (!merge) {
            put_e(&e[k], n, RAS_REGS | RAS_SCROLL | RAS_PAL | RAS_MARK);
            e[k].haze = rn; e[k].haze_n = 6;
            e[k].r26 = ring ? 0 : VDP_R26(256u); e[k].r27 = 0;
            sky_pal(&e[k], 0); k++;
        }
    }
    /* ロゴの 8 帯(ページ 2 の 64..127 行)。出撃の終わりには表示リングの空へ */
    for (i = 0; i < LOGO_BANDS; i++) {
        u8 line = (u8)(LOGO_Y + i * 8);
        RasEntry *b = put_e(&e[k], line, RAS_SCROLL | RAS_PAL);
        if (ring_logo) {
            u16 x = bg_layer_x(line >= 100 ? 0 : 2);
            b->r26 = VDP_R26(x); b->r27 = VDP_R27(x);
            sky_pal(b, (u8)(i < 4 ? 4 : i < 6 ? 5 : 6));
        } else {
            u16 x = (u16)band_x[i];
            b->r26 = VDP_R26(x); b->r27 = VDP_R27(x);
            sky_pal(b, (u8)(i < 4 ? 4 : i < 6 ? 5 : 6));           /* ロゴの地(色 1)= その高さの空の色 */
        }
        if (i == 0 && merge) {                                   /* n 行の項目をまとめた(SCREEN5 へ戻すのもここ) */
            b->flags |= RAS_REGS | RAS_MARK; b->haze = rn; b->haze_n = 6;
        } else if (i == 0 && !ring_logo) {                       /* ロゴの区間はページ 2|3 */
            rr[0] = 2; rr[1] = 0x7F;
            b->flags |= RAS_REGS; b->haze = rr; b->haze_n = 1;
        }
        if (i == 4) {                                            /* 96 行: 下の帯の属性表 */
            rb[0] = 5; rb[1] = spr_regs.r5b; rb[2] = 11; rb[3] = 3;
            b->flags |= RAS_REGS; b->haze = rb; b->haze_n = 2;
        }
        k++;
    }
    /* 128 行: 表示リング(雲)に戻す。色 0 も黒へ戻す */
    {
        static u8 rring[2][4];
        u8 *r = rring[bi];
        u16 x = bg_layer_x(1);
        r[0] = 2; r[1] = 0x3F; r[2] = 23; r[3] = g_vscroll;
        put_e(&e[k], 128, RAS_REGS | RAS_SCROLL | RAS_PAL);
        e[k].haze = r; e[k].haze_n = 2; e[k].r26 = VDP_R26(x); e[k].r27 = VDP_R27(x);
        sky_pal(&e[k], 6); k++;
    }
    {
        u16 x = bg_layer_x(2);
        put_e(&e[k], 150, RAS_SCROLL);
        e[k].r26 = VDP_R26(x); e[k].r27 = VDP_R27(x); k++;
    }
    bg_fill_hud(&e[k], bi);
    if (g_phase == PH_TITLE) {                                    /* PUSH SPACE KEY の点滅(色 11) */
        u8 on = (g_pt & 32) ? 0 : 7;
        e[k].flags |= RAS_PAL; e[k].pidx = 11; e[k].prb = (u8)((on ? 6 : 0) << 4 | on); e[k].pg = on;
    } else {
        e[k].flags |= RAS_PAL; e[k].pidx = 11; e[k].prb = 0x67; e[k].pg = 7;
    }
    k++;
    ras_commit(k);
    g_split_shown = n;
}

void title_start(void) { cf_on = 0; cf_k = 0; cf_dirty = 0; cf_keys = 0; cf_bgm = 0; cf_row = 0; cf_cur = 0; enter(PH_LOGO); }
