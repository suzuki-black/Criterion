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
static u8  v13;                   /* 色 13 に書いた値の目印(タイトルに入るときに 0xFF。title_logo_pal) */
static s16 sh_x[8], sh_y[8];      /* 出撃: ロゴが砕けた破片(拡大スプライト) */
static s8  sh_vx[8], sh_vy[8];
static u8  loop_keep;             /* 1 = 2 周目の出撃(game_start しない) */
static u8  hit_done;              /* ロゴが組み上がったオケヒを鳴らした */
static u16 idle_t;                /* 最後に触ってからのフレーム(デモへ) */

static u8 regs_top[2][16], regs_n[2][14], regs_ring[2][4], regs_b[2][4];

/* ---- 隠しコンフィグ: タイトルで 上 上 下 下 左 右 左 右 B A(B と A はキーボードの B・A キー) ----
   HUD の帯の 2 行に出す。上下で項目、左右で値、SPACE で決定。★オーバレイの変数は初期化されないので title_start で */
enum { CF_INV, CF_STAGE, CF_BGM, CF_SE, CF_PCM, CF_MON, CF_EXIT, CF_N };   /* SE / PCM: 試聴(SOUND TEST)、MON: VDP モニター */
static u8 cf_on, cf_k, cf_item, cf_bgm, cf_se, cf_pcm, cf_dirty, cf_keys, cf_row, cf_cur;   /* cf_cur: 書いている行(bit) */
static const u8 cf_seq[10] = { IN_UP, IN_UP, IN_DOWN, IN_DOWN, IN_LEFT, IN_RIGHT, IN_LEFT, IN_RIGHT, 0x80, 0x40 };   /* 0x80 = B、0x40 = A */
static const char *const cf_song[SND_NSONG + 1] = {
    "M01", "M02", "M03", "M04", "M05", "M06", "M07", "M08", "M09", "M10", "M11", "M12", "M13", "M14A", "M14B", "M14C",
    "M15", "M16", "M17", "J01", "J02", "M18", "M08C", "M10X", "M06R", "M01R", "STOP" };
static char cf_line[20];
static const char *const cf_pcmn[11] = { "P01", "P02", "P03", "P05", "P06", "V01", "V02", "V03", "V04", "V05", "V06" };
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
    if (cf_item == CF_INV) { cf_put("INVINCIBLE", 1); cf_put(g_dbg_invul == 1 ? "ALL" : g_dbg_invul ? "BULLET" : "OFF", 12); }   /* ALL = 何にも / BULLET = 敵弾だけ */
    else if (cf_item == CF_STAGE) { cf_put("STAGE", 1); cf_line[14] = (char)('0' + (g_start_stage ? g_start_stage : 1)); }
    else if (cf_item == CF_BGM) { cf_put("BGM", 1); cf_put(cf_song[cf_bgm], 14); }
    else if (cf_item == CF_SE) { cf_put("SE", 1); cf_line[14] = (char)('0' + (cf_se + 1) / 10); cf_line[15] = (char)('0' + (cf_se + 1) % 10); }
    else if (cf_item == CF_PCM) { cf_put("PCM", 1); cf_put(cf_pcmn[cf_pcm], 14); }
    else if (cf_item == CF_MON) { cf_put("VDP MONITOR", 1); cf_put(g_vdp_mon ? "ON" : "OFF", 14); }
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
        if (cf_item == CF_INV) g_dbg_invul = r ? (u8)(g_dbg_invul < 2 ? g_dbg_invul + 1 : 0) : (u8)(g_dbg_invul ? g_dbg_invul - 1 : 2);   /* OFF → ALL → BULLET */
        else if (cf_item == CF_STAGE) {
            u8 s = g_start_stage ? g_start_stage : 1;
            s = r ? (u8)(s < 8 ? s + 1 : 1) : (u8)(s > 1 ? s - 1 : 8);
            g_start_stage = s;
        } else if (cf_item == CF_BGM) cf_bgm = r ? (u8)(cf_bgm < SND_NSONG ? cf_bgm + 1 : 0) : (u8)(cf_bgm ? cf_bgm - 1 : SND_NSONG);
        else if (cf_item == CF_SE) cf_se = r ? (u8)(cf_se + 1 < SFX_COUNT - 1 ? cf_se + 1 : 0) : (u8)(cf_se ? cf_se - 1 : SFX_COUNT - 2);
        else if (cf_item == CF_PCM) cf_pcm = r ? (u8)(cf_pcm < 10 ? cf_pcm + 1 : 0) : (u8)(cf_pcm ? cf_pcm - 1 : 10);
        else if (cf_item == CF_MON) { g_vdp_mon = (u8)!g_vdp_mon; g_ras_busy_border = g_vdp_mon ? 15 : 0; if (!g_vdp_mon) vdp_wreg(7, 0); }
        cf_dirty |= 1; if (cf_cur == 1) cf_dirty |= 0x10;
    }
    if (g_in_edge & IN_A) {
        if (cf_item == CF_BGM) bgm_play(cf_bgm < SND_NSONG ? cf_bgm : SND_NONE);
        else if (cf_item == CF_SE) sfx((u8)(cf_se + 1));
        else if (cf_item == CF_PCM) pcm_req(cf_pcm);
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
        g_split_n = 0; ring_logo = 0; v13 = 0xFF; hit_done = 0; idle_t = 0;
        g_scroll_speed = 1;
        bg_mix(0);
        vdp_set_pal(15, 7, 7, 7);
        infx_title(1);                                        /* ロゴの下で ∞ が回る(ROTOR) */
    }
    if (ph == PH_SORTIE) {
        s16 y = (s16)(150 + (g_sin64[(u8)(pt_prev >> 1) & 63] >> 4));   /* タイトルで最後に描いた位置 */
        wiped = 64;                                       /* ページ 0 の 0..63 行に地平線の偶数バイトが入っている */
        for (i = 0; i < 8; i++) {                         /* ロゴの文字の位置から外へ飛び散る破片 */
            sh_x[i] = (s16)(12 + i * 30); sh_y[i] = (s16)(66 + (i & 3) * 14);
            sh_vx[i] = (s8)((sh_x[i] - 128) >> 4); sh_vy[i] = (s8)(((sh_y[i] - 96) >> 3) - 2);
        }
        if (!loop_keep) game_start();                     /* 2 周目の出撃は引き継ぐ */
        else infx_reset();                                /* ★2 周目はタイトルの ∞ を止める(game_start が止めるのを通らず、2 周目に ∞ が
                                                             出続けた。遊んでの指摘) */
        loop_keep = 0;
        g_pl.x = FX(40); g_pl.y = FX(y); g_pl.invul = 0;      /* タイトルで飛んでいた位置からそのまま操作 */
        g_pl.alive = 1;
    }
}

/* 寄せる: 現在値を目標へ 1/4 ずつ(最低 1) */
static s16 ease(s16 v, s16 target) {
    s16 d = (s16)(target - v);
    s16 step = (s16)(d / 4);
    if (!step) step = d > 0 ? 1 : d < 0 ? -1 : 0;
    return (s16)(v + step);
}

static void scene_update(void);
static void scene_draw(void);
static void scene_vram(void);
static void scene_build(void);
static u8 tm;

void title_update(void) {
    u8 i;
    if (g_phase == PH_LOGO) {
        if (g_pt < 48) { if (reveal < TDF_LINE + 48) reveal++; }
        else if (g_pt >= 90 && fade15 && !(g_pt & 3)) fade15--;
        if (g_pt >= 120) enter(PH_TITLE);                     /* 2 秒(指示書 4.2) */
        return;
    }
    if (g_phase == PH_TITLE) {
        if (tm) { scene_update(); return; }                   /* ランキング・ネームエントリー・巻き戻し */
        if (g_split_n < YJK_H && g_pt >= 4) g_split_n = (u8)(g_split_n < 8 ? 8 : g_split_n + 2);   /* 地平線の帯が上から伸びる */
        for (i = 0; i < LOGO_BANDS; i++)
            if (g_pt >= 24 + i * 6) band_x[i] = ease(band_x[i], 0);
        if (!hit_done && !band_x[LOGO_BANDS - 1] && !band_x[LOGO_BANDS - 2]) { hit_done = 1; pcm_req(PCM_P01); }   /* 組み上がった瞬間にオケヒ */
        yjk_q++;
        if (g_in) idle_t = 0; else idle_t++;
        if (cf_update()) return;                              /* 隠しコンフィグ */
        if ((g_in_edge & IN_A) && g_pt >= 30) { g_demo = 0; enter(PH_SORTIE); }
        else if (idle_t >= 900 && !cf_on) {                  /* 最後に触ってから 15 秒でデモ(指示書 4.2)。1・3・4・5 面を順に */
            static const u8 demo_st[4] = { 1, 3, 4, 5 };
            g_demo_save = g_start_stage; g_start_stage = demo_st[g_demo_k & 3]; g_demo_k++;
            g_demo = 1; enter(PH_SORTIE);
        }
        return;
    }
    if (g_phase == PH_SORTIE) {
        if (g_pt < 48) for (i = 0; i < 8; i++) { sh_x[i] += sh_vx[i]; sh_y[i] += sh_vy[i]; if (!(g_pt & 3)) sh_vy[i]++; }   /* 破片 */
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
    if (tm) { scene_draw(); return; }
    if (g_phase == PH_SORTIE) {                               /* ロゴが砕けた破片(ロゴの帯の間は拡大。title_build の R#1) */
        u8 i;
        if (g_pt < 48) for (i = 0; i < 6; i++) spr_put(sh_x[i], sh_y[i], PAT_SHARD, G_ENEMY, 0);
        return;
    }
    if (g_phase == PH_TITLE) {
        s16 y = (s16)(150 + (g_sin64[(u8)(g_pt >> 1) & 63] >> 4));
        spr_put(40, y, PAT_SHIP_A, G_SHIP_A, 0);
        spr_put(40, y, PAT_SHIP_B, G_SHIP_B, 0);
    }
}

/* 下の区間でする VRAM の仕事(main.c が RAS_MARK を待ってから呼ぶ) */
void title_vram(void) {
    if (tm) { scene_vram(); return; }
    /* タイトルの文字(この時点ではまだ分割していない)。1 フレームに 1 行(まとめるとフレームが落ちた) */
    /* ★HUD 帯の 1 行目は場面ごとに長さの違う文字を書くので、空白で埋めて x 48..207 を覆う(短い文字を上書きしていた間は、前の場面の
       文字の端が残って「SIPUSH SPACE KEY:K」「SPACE REWIND} BACK」と崩れた。遊んでの指摘) */
    if (g_phase == PH_TITLE && g_pt == 1) font_text(HUD_PAGE, 48, HUD_ROW1, 11, 0, "   PUSH SPACE KEY   ");
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

/* ロゴの帯 4・5 の地(色 13)を空の色に固定する。ロゴが雲の表示に替わったら爆発の色(bg.c と同じ)へ戻す */
static void title_logo_pal(void) {
    const u8 *c = bg_sky(5);
    u8 v = ring_logo ? 0xFE : (u8)((c[0] << 5) ^ (c[1] << 2) ^ c[2]);
    if (v == v13) return;
    v13 = v;
    if (ring_logo) vdp_set_pal(13, 6, 3, 2);
    else vdp_set_pal(13, c[0], c[1], c[2]);
}


/* ======== ランキング・ネームエントリー・巻き戻し(指示書 4.3 / 4.4)。PH_TITLE の中の場面 ========
   画面の 32..111 行にページ 2|3 の 128..207 行を見せ(R#23 = 96)、上下は夜の雲海。表・文字はページ 2 に書く。
   ・ネームエントリー: 32 字の文字盤をページ 2|3 の 128..135 行に 16 ドットおきに並べ、帯の横スクロールで回す
     (512 ドットで一周するので継ぎ目がない)。左右で回し、SPACE で決め、B で 1 字戻す。3 字で決めるか 30 秒で終わる。
     下の雲海では測定線が自機の残骸をなぞる。
   ・巻き戻し: 雲海を速く逆に流し、走査線のノイズ(帯ごとに横位置と色 0 を乱す)を重ねて、タイトルの地平線へ戻る。
   ・ランキング: 上位 10 件を 1 行ずつの帯で右から滑り込ませ、1 位の色(9)を虹色に回す。終わったらロゴが組み上がる。 */
enum { TM_TITLE, TM_NAME, TM_REWIND, TM_RANK };
#define SC_LINE 32
#define SC_R23  96                /* 画面の 32 行 = ページ 2|3 の 128 行(★40 から始めると 10 位の行が雲海の切り替えに掛かった) */
#define SC_END  120
/* ★ネームエントリーの文字盤はページ 2|3 の 130..137 行(画面の 34..41 行)。文字盤の帯だけ横にずらし、横位置を 0 に戻す項目は
   46 行に離して置く(帯の切り替えは割込みの遅れで 1〜2 行前後する。文字盤の 8 行のすぐ下の 40 行で戻していた間は、
   文字の下の 2 行が横位置 0 で出て点々に崩れ、読めなかった。遊んでの指摘) */
#define NM_DIAL 130
#define NM_X0   46
static u8 sel, nm[3], nm_pos, nm_drawn, vjob, rep;
static u16 tm_t, dial_x;
static s16 rk_x[RANK_N];
static u8 regs_s0[2][12], regs_s1[2][4], regs_s2[2][4], regs_se[2][4], regs_s3[2][2];
static char sc_buf[20];
static const char dial_ch[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.- 012";
static const u8 rainbow[8][3] = { { 7, 0, 0 }, { 7, 4, 0 }, { 7, 7, 0 }, { 0, 7, 0 }, { 0, 7, 7 }, { 0, 2, 7 }, { 5, 0, 7 }, { 7, 0, 5 } };

static void rank_init(void) {
    static const char dn[] = "TDFINFCRIMSXOPLPSGYJKSCCVDPRAM";
    u8 i;
    if (g_rank_ok == 0x5A) return;
    for (i = 0; i < RANK_N; i++) {
        g_rank_nm[i][0] = dn[i * 3]; g_rank_nm[i][1] = dn[i * 3 + 1]; g_rank_nm[i][2] = dn[i * 3 + 2];
        g_rank_sc[i] = (u16)(5000 - i * 450);
    }
    g_rank_ok = 0x5A;
}

/* 得点(10 点単位)を 6 桁に */
static void score6(char *d, u16 v) {
    u8 i;
    for (i = 5; i > 0; i--) { d[i - 1] = (char)('0' + v % 10); v /= 10; }
    d[5] = '0';
}

static u16 dial_target(void) { return (u16)((sel * 16u - 124u) & 511u); }

static void scene(u8 m) {
    u8 i;
    g_phase = PH_TITLE; g_pt = 0; tm = m; tm_t = 0; vjob = 1; rep = 0;
    g_title_mode = m == TM_NAME ? 1 : m == TM_REWIND ? 2 : 0;
    g_hud_on = 0; g_split_n = 0; g_split_shown = 0; ring_logo = 0;
    infx_reset(); bg_mix(0); vdp_set_pal(15, 7, 7, 7);
    g_scroll_speed = m == TM_REWIND ? -24 : 1;
    sel = 0; dial_x = dial_target(); nm_pos = 0; nm[0] = nm[1] = nm[2] = ' '; nm_drawn = 0xFF;
    /* ★ランキングの行は滑らせない(rk_x = 0。上から 1 行ずつ書いた順に出る)。帯ごとに右から滑り込ませていた間は、滑る帯の右側に
       ページ 3(紋章・スプライトの表)が見え、帯の切り替えが割込みの遅れで文字の上の行に掛かって「0」が「A」に崩れ、画面が
       乱れて見えた(遊んでの指摘) */
    for (i = 0; i < RANK_N; i++) rk_x[i] = 0;
    vdp_write_addr17(1, (u16)(0xF800u + PAT_SCAN * 32u)); for (i = 0; i < 32; i++) VDP_DAT = i < 16 ? 0x01 : 0x80;   /* 測定線 */
}

static void to_title(void) { tm = TM_TITLE; g_title_mode = 0; enter(PH_TITLE); }

static void name_finish(void) {
    u8 p = 0, i;
    while (p < RANK_N && g_last_score <= g_rank_sc[p]) p++;
    if (p < RANK_N) {
        for (i = RANK_N - 1; i > p; i--) { g_rank_sc[i] = g_rank_sc[i - 1]; g_rank_nm[i][0] = g_rank_nm[i - 1][0]; g_rank_nm[i][1] = g_rank_nm[i - 1][1]; g_rank_nm[i][2] = g_rank_nm[i - 1][2]; }
        g_rank_sc[p] = g_last_score;
        for (i = 0; i < 3; i++) g_rank_nm[p][i] = nm[i] == ' ' ? '.' : nm[i];
    }
    sfx(SFX_CONFIRM);
    scene(TM_REWIND);
}

static void name_update(void) {
    u8 e = g_in_edge, mv = 0;
    s16 d;
    if (g_in & (IN_LEFT | IN_RIGHT)) {                        /* 押しっぱなしは 16 フレーム後から 6 フレームおき */
        if (e & (IN_LEFT | IN_RIGHT)) { rep = 16; mv = 1; }
        else if (!--rep) { rep = 6; mv = 1; }
    }
    if (mv) { sel = (u8)((sel + ((g_in & IN_RIGHT) ? 1 : 31)) & 31); sfx(SFX_TICK); }
    if (e & IN_A) {
        if (nm_pos < 3) { nm[nm_pos++] = dial_ch[sel]; sfx(SFX_ITEM); }
        else { name_finish(); return; }
    }
    if ((e & IN_B) && nm_pos) { nm[--nm_pos] = ' '; sfx(SFX_DENY); }
    d = (s16)((dial_target() - dial_x) & 511u);
    if (d >= 256) d -= 512;
    if (d) dial_x = (u16)((dial_x + (d / 3 ? d / 3 : (d > 0 ? 1 : -1))) & 511u);   /* 文字盤が回って止まる */
    if (tm_t >= 1800) { while (nm_pos < 3) nm[nm_pos++] = '.'; name_finish(); }
}

static void scene_update(void) {
    u8 i;
    tm_t++;
    if (tm_t == 2) { *(volatile u8 *)0xF3E0 |= 0x40; vdp_wreg(1, *(volatile u8 *)0xF3E0); }   /* 読み込みの間止めていた表示を戻す
                                                                 (ラスターリストが効いてから。起動ロゴは main.c が戻す) */
    if (tm == TM_NAME) name_update();
    else if (tm == TM_REWIND) { if (tm_t >= 150) to_title(); }
    else {
        for (i = 0; i < RANK_N; i++) if (tm_t >= 8 + i * 4) rk_x[i] = ease(rk_x[i], 0);
        if (tm_t >= 480 || (tm_t > 60 && (g_in_edge & IN_A))) to_title();
    }
}

static void scene_draw(void) {
    if (tm == TM_NAME) {
        u8 bx = (u8)(100 + ((tm_t >> 1) & 127));
        spr_put(150, 156, PAT_SHIP_A, G_MISC, CS_DIM);                          /* 残骸 */
        if (bx < 216) { spr_put(bx, 136, PAT_SCAN, G_EBUL, 0); spr_put(bx, 152, PAT_SCAN, G_EBUL, 0); spr_put(bx, 168, PAT_SCAN, G_EBUL, 0); }   /* 測定線 */
    } else if (tm == TM_RANK && tm_t < 90) {                  /* 自機が遠ざかる */
        spr_put((s16)(40 + tm_t * 3), (s16)(150 - (tm_t >> 1)), PAT_SHIP_A, G_SHIP_A, 0);
        spr_put((s16)(40 + tm_t * 3), (s16)(150 - (tm_t >> 1)), PAT_SHIP_B, G_SHIP_B, 0);
    }
}

/* 1 フレームに 1 つの仕事(文字は 1 行ずつ) */
static void scene_vram(void) {
    u8 j = vjob, i;
    if (!j) {
        if (tm == TM_NAME && nm_drawn != nm_pos) {           /* 名前の 3 字 */
            sc_buf[0] = nm[0]; sc_buf[1] = ' '; sc_buf[2] = nm[1]; sc_buf[3] = ' '; sc_buf[4] = nm[2]; sc_buf[5] = 0;
            vdp_cmd_wait(); font_text(2, 108, (u8)(NM_DIAL + 54), 15, 0, sc_buf); nm_drawn = nm_pos;
        }
        return;
    }
    vjob++;
    if (j == 1) { vdp_hmmv(0, 512 + 128, 256, 84, 0x00); return; }           /* ページ 2 の 128..211 行 */
    if (j == 2) { vdp_hmmv(0, 768 + 128, 256, 60, 0x00); return; }           /* ページ 3 の 128..187 行(紋章を消す。起動ロゴは次の読み込みで戻る) */
    vdp_cmd_wait();
    if (tm == TM_NAME) {
        if (j <= 4) {                                         /* 文字盤: ページ 2 に 0..15、ページ 3 に 16..31(字と空白を交互に 32 字 = 256 ドット) */
            u8 b = (u8)((j - 3) * 16);
            {
                static char dl[33];
                for (i = 0; i < 16; i++) { dl[i * 2] = dial_ch[b + i]; dl[i * 2 + 1] = ' '; }
                dl[32] = 0;
                font_text((u8)(j - 1), 0, NM_DIAL, 11, 0, dl);
            }
            return;
        }
        if (j == 5) { font_text(2, 120, (u8)(NM_DIAL + 16), 13, 0, "--"); return; }   /* 選んでいる字の下線(文字盤の中央。★スプライトは文字盤の帯と
                                                                 雲海で R#23 が違い、雲海のほうに出た) */
        if (j == 8) { font_text(2, 68, (u8)(NM_DIAL + 24), 15, 0, "ENTER YOUR NAME"); return; }
        if (j == 6) {
            sc_buf[0] = 'S'; sc_buf[1] = 'C'; sc_buf[2] = 'O'; sc_buf[3] = 'R'; sc_buf[4] = 'E'; sc_buf[5] = ' '; sc_buf[6] = ' ';
            score6(sc_buf + 7, g_last_score); sc_buf[13] = 0;
            font_text(2, 76, (u8)(NM_DIAL + 38), 14, 0, sc_buf); return;
        }
        if (j == 7) { font_text(HUD_PAGE, 48, HUD_ROW1, 11, 0, "  SPACE SET  B BACK "); return; }
    } else if (tm == TM_RANK) {
        if (j <= 12) {
            u8 k = (u8)(j - 3);
            sc_buf[0] = k == 9 ? '1' : ' '; sc_buf[1] = (char)('0' + (k == 9 ? 0 : k + 1)); sc_buf[2] = ' '; sc_buf[3] = ' ';
            sc_buf[4] = g_rank_nm[k][0]; sc_buf[5] = g_rank_nm[k][1]; sc_buf[6] = g_rank_nm[k][2];
            sc_buf[7] = ' '; sc_buf[8] = ' ';
            score6(sc_buf + 9, g_rank_sc[k]); sc_buf[15] = 0;
            font_text(2, 68, (u8)(128 + k * 8), k ? (k < 3 ? 11 : 15) : 9, 0, sc_buf);
            return;
        }
        if (j == 13) { font_text(HUD_PAGE, 48, HUD_ROW1, 11, 0, " TOP 10 MEASUREMENTS"); return; }
    } else {
        if (j == 3) { font_text(HUD_PAGE, 48, HUD_ROW1, 13, 0, "       REWIND       "); return; }
    }
    vjob = 0;
}

/* ラスターリスト */
static void scene_build(void) {
    RasEntry *e = ras_back();
    u8 bi = ras_back_index(), k = 0, i;
    u8 *r0 = regs_s0[bi], *r1 = regs_s1[bi], *r2 = regs_s2[bi], *re = regs_se[bi], *r3 = regs_s3[bi];
    u16 bx = bg_layer_x(1);
    /* ★上の帯はリングの 64 行から先(空)を見せる。リングの 0..63 行(ページ 0)にはタイトルの地平線の YJK の偶数バイトが入っていて、
       そのまま見せると巻き戻しで流したときに色の縦じまに、ネームエントリーでは右上の四角に見えた(遊んでの指摘「壊れている」) */
    r0[0] = 0; r0[1] = R0_G4; r0[2] = 25; r0[3] = R25_G4; r0[4] = 8; r0[5] = R8_SPR;
    r0[6] = 2; r0[7] = 0x3F; r0[8] = 23; r0[9] = (u8)(g_vscroll + 64); r0[10] = 5; r0[11] = spr_regs.r5a;
    r3[0] = 23; r3[1] = g_vscroll;
    put_e(&e[k], 0, RAS_REGS | RAS_SCROLL | RAS_PAL); e[k].haze = r0; e[k].haze_n = 6;
    e[k].r26 = VDP_R26(bx); e[k].r27 = VDP_R27(bx); sky_pal(&e[k], 0); k++;    /* 空(色 1)は夜の色 */
    r2[0] = 5; r2[1] = spr_regs.r5b; r2[2] = 11; r2[3] = 3;   /* 96 行: 下の帯の属性表 */
    if (tm == TM_REWIND) {                                    /* 走査線のノイズ: 帯ごとに横位置を乱し、ときどき空(色 1)を白く飛ばす
                                                                 (★色 0 は外枠の色でもあり、光らせると画面の外まで灰色の帯が出た) */
        u8 l;
        for (l = 12; l < 180; l += 12) {
            u16 x = (u16)(bx + (rnd8() & 15) - 8);
            put_e(&e[k], l, RAS_SCROLL | RAS_PAL); e[k].r26 = VDP_R26(x); e[k].r27 = VDP_R27(x);
            sky_pal(&e[k], (u8)(l / 26));
            if (rnd8() < 40) { u8 f = (u8)(4 + (rnd8() & 3)); e[k].prb = (u8)((f << 4) | f); e[k].pg = f; }
            if (l == 72) { e[k].flags |= RAS_REGS; e[k].haze = r3; e[k].haze_n = 1; }   /* 72 行から元の縦位置(リングの 72 行〜) */
            if (l == 96) { e[k].flags |= RAS_REGS; e[k].haze = r2; e[k].haze_n = 2; }
            k++;
        }
    } else {
        r1[0] = 2; r1[1] = 0x7F; r1[2] = 23; r1[3] = SC_R23;
        if (tm == TM_NAME) {                                   /* 文字盤の帯(32 行)、横位置を戻す(46 行)、下の帯の属性表(96 行) */
            put_e(&e[k], SC_LINE, RAS_SCROLL | RAS_REGS); e[k].r26 = VDP_R26(dial_x); e[k].r27 = VDP_R27(dial_x);
            e[k].haze = r1; e[k].haze_n = 2; k++;
            put_e(&e[k], NM_X0, RAS_SCROLL); e[k].r26 = 0; e[k].r27 = 0; k++;
            put_e(&e[k], 96, RAS_SCROLL | RAS_REGS); e[k].r26 = 0; e[k].r27 = 0; e[k].haze = r2; e[k].haze_n = 2; k++;
        } else
        for (i = 0; i < RANK_N; i++) {                         /* 32..111 行: ページ 2|3 の 8 行ずつの帯 */
            u16 x = tm == TM_RANK ? (u16)rk_x[i] : (i == 0 ? dial_x : 0);
            put_e(&e[k], (u8)(SC_LINE + i * 8), RAS_SCROLL); e[k].r26 = VDP_R26(x); e[k].r27 = VDP_R27(x);
            if (i == 0) { e[k].flags |= RAS_REGS; e[k].haze = r1; e[k].haze_n = 2; }
            if (i == 8) { e[k].flags |= RAS_REGS; e[k].haze = r2; e[k].haze_n = 2; }   /* 96 行 */
            if (tm == TM_RANK && i < 2) {                     /* 1 位の色 9 を虹色に回し、次の帯で戻す */
                const u8 *c = rainbow[(tm_t >> 2) & 7];
                e[k].flags |= RAS_PAL; e[k].pidx = 9;
                if (i == 0) { e[k].prb = (u8)((c[0] << 4) | c[2]); e[k].pg = c[1]; }
                else { e[k].prb = 0x25; e[k].pg = 3; }
            }
            k++;
        }
        re[0] = 2; re[1] = 0x3F; re[2] = 23; re[3] = g_vscroll;   /* 120 行: 雲海へ戻す */
        put_e(&e[k], SC_END, RAS_REGS | RAS_SCROLL | RAS_PAL); e[k].haze = re; e[k].haze_n = 2;
        e[k].r26 = VDP_R26(bx); e[k].r27 = VDP_R27(bx); sky_pal(&e[k], 6); k++;
    }
    bg_fill_hud(&e[k], bi); k++;
    ras_commit(k);
    g_split_shown = 0;
}

void title_build(void) {
    RasEntry *e = ras_back();
    u8 bi = ras_back_index(), k = 0, i;
    u8 *rt = regs_top[bi], *rn = regs_n[bi], *rr = regs_ring[bi], *rb = regs_b[bi];
    u8 n = g_split_n, merge;

    if (tm) { scene_build(); return; }
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

    title_logo_pal();
    /* 上の帯(SCREEN12)は 244 行で切り替える */
    if (n) {
        u16 yx = yjk_q >> 3;
        rt[0] = 0; rt[1] = R0_G7; rt[2] = 25; rt[3] = R25_YJK; rt[4] = 2; rt[5] = 0x1F;
        rt[6] = 8; rt[7] = R8_NOSP; rt[8] = 23; rt[9] = 0; rt[10] = 5; rt[11] = spr_regs.r5a; rt[12] = 11; rt[13] = 3; rt[14] = 18; rt[15] = R18_G7;
        put_e(&e[k], TOP_SWITCH, RAS_REGS | RAS_SCROLL | RAS_PAL);
        e[k].haze = rt; e[k].haze_n = 8; e[k].r26 = (u8)(VDP_R26(yx) & 31); e[k].r27 = VDP_R27(yx);   /* SP2 なし = 256 幅 */
        e[k].pidx = 11; e[k].prb = 0x67; e[k].pg = 7; k++;   /* ★色 11 を戻す(HUD 帯で PUSH SPACE KEY を点滅させる色。ロゴの文字の
                                                              2・3 段目も色 11 なので、戻さないとロゴの一部が一緒に点滅した) */
    }
    /* n 行: SCREEN5 へ。タイトル中はページ 3(黒)、出撃中は表示リング(描き戻した空)。
       ★ロゴの 64 行に近い(RAS_GAP 未満)ときは、ロゴの最初の帯の項目にまとめる(merge) */
    merge = n >= LOGO_Y - RAS_GAP;
    {
        u8 ring = g_phase == PH_SORTIE && !(merge && !ring_logo);
        rn[0] = 0; rn[1] = R0_G4; rn[2] = 25; rn[3] = R25_G4; rn[4] = 8; rn[5] = R8_SPR;
        rn[6] = 2; rn[7] = ring ? 0x3F : 0x7F; rn[8] = 23; rn[9] = g_vscroll;
        rn[10] = 5; rn[11] = spr_regs.r5a; rn[12] = 18; rn[13] = R18_SAVE;
        if (!merge) {
            put_e(&e[k], n, RAS_REGS | RAS_SCROLL | RAS_PAL | RAS_MARK);
            e[k].haze = rn; e[k].haze_n = 7;
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
            /* ロゴの地 = その高さの空の色。帯 0..3 は色 1(64 行で書く。地平線の中で効く)、帯 4・5 は色 13(固定。
               title_logo_pal)、帯 6・7 は色 1 を 112 行で書き換える(早めに効いて 110・111 行に掛かるが、そこの地は色 13) */
            if (i == 0) sky_pal(b, 4);
            else if (i == 6) sky_pal(b, 6);
            else b->flags &= (u8)~RAS_PAL;
        }
        if (i == 0 && merge) {                                   /* n 行の項目をまとめた(SCREEN5 へ戻すのもここ) */
            b->flags |= RAS_REGS | RAS_MARK; b->haze = rn; b->haze_n = 7;
        } else if (i == 0 && !ring_logo) {                       /* ロゴの区間はページ 2|3 */
            rr[0] = 2; rr[1] = 0x7F;
            mag_pair(rr + 2, g_phase == PH_SORTIE && g_pt < 48);   /* 出撃の破片はロゴの区間で拡大(128 行で戻す) */
            b->flags |= RAS_REGS; b->haze = rr; b->haze_n = 2;
        }
        if (i == 4) {                                            /* 96 行: 下の帯の属性表 */
            rb[0] = 5; rb[1] = spr_regs.r5b; rb[2] = 11; rb[3] = 3;
            b->flags |= RAS_REGS; b->haze = rb; b->haze_n = 2;
        }
        if ((i & 3) == 3) pcmx_service();                        /* ★PCM の送出口(この関数だけで 1ms 近く止めていた) */
        k++;
    }
    /* 128 行: 表示リング(雲)に戻す。色 0 も黒へ戻す */
    {
        static u8 rring[2][6];
        u8 *r = rring[bi];
        u16 x = bg_layer_x(1);
        r[0] = 2; r[1] = 0x3F; r[2] = 23; r[3] = g_vscroll; mag_pair(r + 4, 0);
        put_e(&e[k], 128, RAS_REGS | RAS_SCROLL | RAS_PAL);
        e[k].haze = r; e[k].haze_n = 3; e[k].r26 = VDP_R26(x); e[k].r27 = VDP_R27(x);
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

void title_start(void) {
    u8 a = g_after_over;
    cf_on = 0; cf_k = 0; cf_dirty = 0; cf_keys = 0; cf_bgm = 0; cf_se = 0; cf_pcm = 0; loop_keep = 0; cf_row = 0; cf_cur = 0; tm = TM_TITLE; g_title_mode = 0;
    g_after_over = AO_BOOT;
    g_far = 0;                                                /* 遠景を外す(前の面のオーバレイの変数を指したまま。本体の常駐に空きが無いのでここで) */
    rank_init();
    if (a == AO_OVER) scene(g_last_score > g_rank_sc[RANK_N - 1] ? TM_NAME : TM_REWIND);   /* ゲームオーバー → ネームエントリー → 巻き戻し */
    else if (a == AO_DEMO) scene(TM_RANK);                                                  /* デモの後はランキング */
    else if (a == AO_LOOP) {                                  /* エンディングから: 組み上がったロゴと地平線から出撃 */
        u8 i;
        loop_keep = 1;
        enter(PH_TITLE);
        for (i = 0; i < LOGO_BANDS; i++) band_x[i] = 0;
        g_split_n = YJK_H; hit_done = 1;
        enter(PH_SORTIE);
        *(volatile u8 *)0xF3E0 |= 0x40; vdp_wreg(1, *(volatile u8 *)0xF3E0);   /* 読み込みの間止めていた表示を戻す */
    }
    else enter(PH_LOGO);
}
