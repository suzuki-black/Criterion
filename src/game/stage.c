/* stage.c — 面の土台(指示書 5)。縦に長い世界と表示リング、列の流し込み、ラスターリストの骨組み。
   面ごとの中身(区間・地図・帯・ボス)はオーバレイ(src/ovl/s1 ほか。ovl.h の入口)が受け持つ。

   縦の座標 s(game.h): 上の区間(空中戦の空と雲・高い空)が s = -188..-1、地面(短冊で描く区間)が s = 0..187。
   表示リング(ページ 0|1、512x256)の行 = (188 + s) & 255。画面には 188 行しか出ないので、リングの残りの
   68 行と、画面から出た行を、次に見える s の内容へ描き替えながら進む。
   ・8 行ごとのブロック(32 個)に「今どの s を持っているか」(tag)を持つ。カメラの窓(進む向きに 60 行先まで)
     で欲しい s と違い、かつ画面に出ていないブロックを描き替える。
       地面(s >= 0): ページ 3 の短冊(8x188)から 1 列ずつ HMMM。どの短冊かは地図(g_map、面が読み込む)。
       高い空(UP_SKY): HMMV で空の色(色 1)を塗り、星を打つ。
       空中戦の空と雲(UP_AIR): 起動時に描いたもの(bg.c)をそのまま使う(描き直さない)。
   ・横は 8 ドット(1 列)進むごとに、画面の右の外の列を地図の次の列で描く(地面のブロックだけ)。
   ・★ブロック 23(リングの 184..191 行)は s = -4..3。空中戦の近い雲の底も短冊の上端(雲の底)も色 6 だけ
     なので、描き替えない。
   ・ラスターは s の帯の境目を「画面の行 = s - g_scam」に置く。画面より上に出た境目は 0 行の項目にまとめる。
     項目どうしは RAS_GAP 行以上あける(近い境目は 1 つの項目にまとめるか、後ろへずらす)。 */
void pcmx_service(void);   /* snd.c: PCM 方式 B の送出口 */
#include "game.h"
#include "vdp.h"
#include "raster.h"
#include "spr.h"
#include "rom.h"
#include "assets.h"
#include "ovl.h"
#include "sys.h"
#include "font.h"
#include "ghost.h"

#define S_AIR      (-188)
#define S_AIR2     (-192)          /* 2 面の空中戦。★ブロック 23(s = -4..-1)が画面に出ない位置 */
#define STRIP_Y    768             /* 短冊はページ 3 の 0..187 行 */
#define STRIP_LEN  (188u * 128u)
#define JOB_COLS   8               /* 都市のブロックを 1 フレームに描く列の数(HMMM 1 回 ≒ 0.13ms) */
#define NBLK       32
#define BLK_CONST  23
#define TAG_NONE   0x7FFF
#define TAG_SKY    1024            /* 高い空のブロックの tag = s + TAG_SKY(空中戦の空と区別する) */
#define LOOK_AHEAD 60              /* 進む向きに先に描いておく行数(= 256 - 188 - 8) */

u8  g_sec;
u16 g_wx;
s16 g_scam;
s16 g_scam_q;                      /* g_scam × 16(面が動かす) */
u8  g_sky_t;                       /* 高い空の暗さ(0 = 藍 .. 16 = 黒。面が動かす) */
u16 g_w0;                          /* 地図の列 0 の世界の列(8 ドット単位)。検証(run_game_m3.sh)も読む */
u8  g_map[MAP_MAX];                /* 地図: 列ごとの短冊の番号(面が読み込む) */
u16 g_maplen;
/* ボスの状態(面のオーバレイが動かす。本体・検証も読むので本体に置く) */
u8  g_boss_state, g_boss_hp, g_boss_dmg;
u16 g_lg_on[26];
s16 g_lg_x0;
void boss_damage(u8 n) { g_boss_dmg = (u8)(g_boss_dmg + n); }   /* LIMIT BREAK など(面が次のフレームで受け取る) */

#define w0 g_w0
static s8  dir;                    /* カメラの縦の向き(+1 = 降下、-1 = 上昇) */
static u8  upper_sky;              /* UP_SKY = s < 0 は高い空 */
static s16 tag[NBLK];
static u8  strip_bank;
static u16 strip_src;
static u16 strip_off;              /* 短冊を ROM から読んだバイト数(STRIP_LEN で終わり。0xFFFF = 読まない) */
static u16 col_done;               /* 描いた列の窓 [col_lo, col_done)(64 列まで) */
static u16 col_lo;
static u16 nx_prev;                /* 前のフレームの bg の位置(g_wx を差分で進める) */
u8  g_col_rev;
u8  g_strip_xlat[32];
static u16 redraw_q[8];
static u8  redraw_n;
/* 欲しい列の窓 [lo, lo + 64) の左端: 画面の左端から g_col_rev 列だけ左(前進 0 / 逆走 31 / 前後 16) */
static u16 win_lo(void) { return (u16)((g_wx >> 3) - g_col_rev); }
static u16 cur_col;                /* 流している列 */
static u8  col_b;                  /* その列の次のブロック(NBLK = 列を終えた) */
static u8  jb0, jn, jstep;         /* 描き替え中のブロック(先頭・個数。jn = 0 で仕事なし)と進み */
static s16 js;
static s16 pick_at;                /* 前に描き替えを探して何も無かったときの g_scam(8 行単位) */
static u8  hs_t;                   /* hs_cur を求めたときの sky_t */
static u8  banner_t;               /* 面の開始表示の進み(0 = なし) */
static char banner_text[32];
static u8 banner_fg = 15;          /* 文字の色(7 面の判定文は赤く崩れていく) */       /* ★写しを持つ(呼ぶのは古いオーバレイで、その文字列は次のオーバレイの読み込みで消える) */

/* 画面の矩形 → 表示リング(ページ 0|1)。リングの 512 幅と 256 行の継ぎ目で分ける。
   fill = 0 なら (sx, sy) から HMMM、それ以外は byte で HMMV */
void ring_cmd(u16 sx, u16 sy, s16 x, s16 y, u8 w, u8 h, u8 fill, u8 byte) {
    u16 rx = (u16)((g_wx + (u16)x) & 511u);
    u8 ry = (u8)(y + g_vscroll);
    u8 w1 = w, h1 = h;
    if ((u8)rx <= (u8)(256 - w) && ry <= (u8)(256 - h)) {     /* 継ぎ目をまたがない(ほとんど) */
        u16 dy = (u16)(rx >= 256 ? 256u + ry : ry);
        if (fill) gq_hmmv(rx & 255, dy, w, h, byte); else gq_hmmm(sx, sy, rx & 255, dy, w, h);
        return;
    }
    if (rx < 256 && rx + w > 256) w1 = (u8)(256 - rx);
    else if (rx >= 256 && rx + w > 512) w1 = (u8)(512 - rx);
    if ((u16)ry + h > 256) h1 = (u8)(256 - ry);
    {
        u16 dx = rx & 255, dy = (u16)(rx >= 256 ? 256u + ry : ry);
        if (fill) gq_hmmv(dx, dy, w1, h1, byte); else gq_hmmm(sx, sy, dx, dy, w1, h1);
    }
    if (w1 < w) ring_cmd((u16)(sx + w1), sy, (s16)(x + w1), y, (u8)(w - w1), h1, fill, byte);
    if (h1 < h) ring_cmd(sx, (u16)(sy + h1), x, (s16)(y + h1), w, (u8)(h - h1), fill, byte);
}

/* ---- 面から使う ---- */
void stage_set_upper(u8 kind) { upper_sky = kind; pick_at = 0x7FFF; }
void stage_load_strips(u8 bank, u16 off) { strip_bank = bank; strip_src = off; strip_off = 0; }
u8 stage_redraw_col(u16 wcol) {
    if ((u16)(wcol - col_lo) >= (u16)(col_done - col_lo)) return 1;   /* 窓の外(まだ描いていない。流れてきたときに描く) */
    if (redraw_n >= 8) return 0;
    redraw_q[redraw_n++] = wcol;
    return 1;
}
u8 g_roll;
u8 g_mag;                           /* 拡大スプライトの帯(1 = 上の帯、2 = 下の帯、0 = なし) */

/* 地面のブロックを「何も描いていない」にして、列の流し込みを止める(次の面の短冊を読む前) */
void stage_reset_ground(void) {
    u8 b;
    for (b = 0; b < NBLK; b++) if (b != BLK_CONST && tag[b] >= 0 && tag[b] < 188) tag[b] = TAG_NONE;
    strip_off = 0xFFFF; col_b = NBLK; jn = 0; pick_at = 0x7FFF; g_maplen = 0; redraw_n = 0;
    for (b = 0; b < 32; b++) g_strip_xlat[b] = b;
    g_wx &= 511u;                                             /* 世界の位置を面ごとに戻す(リングの位置は同じ) */
    g_bands_all = 0; g_blk_skip = 0; g_blk_base = 0;
}
/* ---- 7 面の試験(過去の面のオーバレイを g_calib で再校正版として読み直す) ---- */
u8 g_calib, g_loop;
void calib_begin(u8 k) {
    char s[27];
    static const char head[] = "TEST 0  SUBJECT: INFINITAS";
    u8 i;
    for (i = 0; i < sizeof head; i++) s[i] = head[i];
    s[6] = (char)('0' + k);
    stage_banner(s);
    g_hall = 1;
}
/* 判定文は試験が進むほど赤く崩れる(文字を少しずつ壊す) */
u8 calib_out(u16 t) {
    if (t == 1) {
        char s[25];
        static const char res[] = "RESULT: OUT OF TOLERANCE";
        u8 i, k = g_calib;
        for (i = 0; i < sizeof res; i++) s[i] = res[i];
        for (i = 0; i < k * 2 && i < 12; i++) s[8 + ((i * 7) & 15)] = (char)('A' + ((i * 5 + k) % 26));
        stage_banner_c(s, k <= 2 ? 15 : k <= 4 ? 12 : 13);
        enemy_waves(WAVES_NONE);
        g_scroll_speed = 4;                                   /* ボスで止めた世界を流す(次の試験は流れで降りる) */
    }
    if (g_scam_q > -192 * 16) g_scam_q -= 24;                 /* 白い空間へ上がる */
    if (t == 300) {
        u8 k = ++g_calib;
        ovl_request(k <= 6 ? (u8)(OVL_S1 + k - 1) : OVL_S7);
        return 1;
    }
    return 0;
}

void stage_banner_c(const char *text, u8 fg) { stage_banner(text); banner_fg = fg; }
void stage_banner(const char *text) {
    u8 i;
    banner_fg = 15;
    for (i = 0; i < sizeof banner_text - 1 && text[i]; i++) banner_text[i] = text[i];
    banner_text[i] = 0;
    banner_t = 1;
}

/* 1 面の空中戦から(出撃の終わりに title.c が呼ぶ)。面の中身は 1 面のオーバレイを読み込んでから */
void stage_start(void) {
    u8 b;
    g_wx = bg_layer_x(2); nx_prev = g_wx;
    g_scam_q = S_AIR * 16; g_scam = S_AIR; dir = 1;
    upper_sky = UP_AIR; g_sky_t = 0; g_sec = 0; g_maplen = 0; g_roll = 0; g_boss_state = BS_NONE; g_boss_dmg = 0; g_boss_aim = -1;
    for (b = 0; b < NBLK; b++) tag[b] = b < BLK_CONST ? (s16)(b * 8 - 188) : TAG_NONE;
    tag[BLK_CONST] = -4;
    strip_off = 0xFFFF; col_done = 0; col_b = NBLK; jn = 0; pick_at = 0x7FFF; hs_t = 0xFF; banner_t = 0;
    g_col_rev = 0; redraw_n = 0; g_bands_all = 0;
    for (b = 0; b < 32; b++) g_strip_xlat[b] = b;
    enemy_waves(WAVES_AIR);
    ovl_request((u8)(g_start_stage > 1 ? OVL_S1 + g_start_stage - 1 : OVL_S1));   /* 隠しコンフィグの面セレクト */
}

/* ★世界の位置 g_wx は bg の位置(1/4 ドットの u16 = 16384 ドットで一周)の差分で進める。そのまま使うと 5 面の途中で
   16384 → 0 に戻り、列の計算が全部壊れた。表示(R#26/27)は 512 で回るので nx のままでよく、g_wx ≡ nx (mod 512)。
   面ごとに stage_reset_ground で 0..511 へ戻す(u16 でも溢れない) */
void stage_update(void) {
    s16 target = g_scam;
    {
        u16 nx = bg_layer_x(2), d = (u16)(nx - nx_prev) & 0x3FFFu;
        if (d & 0x2000u) d |= 0xC000u;                        /* 14 ビットの符号(逆走) */
        g_wx += d; nx_prev = nx;
    }
    if (g_ovl_ready && g_ovl >= OVL_S1) ovl_update();
    g_scam = (s16)(g_scam_q >> 4);
    if (g_scam != target) dir = g_scam > target ? 1 : -1;
    g_vscroll = (u8)(188 + g_scam);
}

/* ---- 表示リングへの描き込み ----
   ★VDP のコマンド(HMMM は 1 バイト約 5us)を待つと CPU が止まるので、仕事は「次の 1 コマンド」ずつに分け、
     pump が時間の予算(PUMP_BUDGET)の間だけ続けて出す。最後に出したコマンドは次のフレームまでに VDP が
     終える(CPU は待たない)。1 コマンドは最大 3 ブロック(8x24 = 96 バイト ≒ 0.5ms)。 */
#define PUMP_BUDGET 330            /* S1990 タイマのカウント(≒ 20 ライン) */
#define RUN_MAX     3
#define RUN_LAG     12             /* 窓が 8 列以上遅れたら 1 コマンド 12 ブロック(8x96 ≒ 4.6ms)。3 面の最高速
                                      (8 ドット/フレーム)で 3 ブロックずつでは 1 フレーム 1 列に追いつかず、
                                      画面に古い列(リングの 64 列前)が見えた */
static u8 run_max = RUN_MAX;       /* 流している列のコマンドの最大ブロック数 */

static u8 strip_of(u16 wcol) {
    u16 m = (u16)(wcol - w0);
    if (wcol < w0 || m >= g_maplen) return 0;
    return g_strip_xlat[g_map[m] & 31];
}

/* 次のコマンド(c_hmmv = 1 なら HMMV、c_star = 1 なら星を打つ) */
static u16 c_sx, c_sy, c_dx, c_dy, c_nx, c_ny;
static u8  c_hmmv, c_star;

static void cmd_ring(u8 c, u8 b, u16 wcol, s16 s0, u8 n) {
    c_sx = (u16)strip_of(wcol) * 8u; c_sy = (u16)(STRIP_Y + s0);
    c_dx = (u16)((c & 31) * 8u); c_dy = (u16)(((c & 32) ? 256u : 0u) + b * 8u);
    c_nx = 8; c_ny = (u16)n * 8u; c_hmmv = 0; c_star = 0;
}

/* 流す列: 都市のブロックの、リングで連続して s も続く並び(最大 RUN_MAX)ごとに 1 コマンド */
static u8 next_col_cmd(void) {
    for (;;) {
        if (col_b >= NBLK) {
            u16 lo = win_lo();
            if (redraw_n) {                                   /* 描き直しの頼み(窓の中のものだけ) */
                cur_col = redraw_q[--redraw_n];
                if ((u16)(cur_col - col_lo) >= (u16)(col_done - col_lo)) continue;
            } else if ((s16)(col_done - (u16)(lo + 64)) < 0) {   /* 右へ: 画面の左の外の列へ次の列 */
                if ((s16)(col_done - lo) < 0) col_done = lo;  /* 遅れて窓の後ろへ流れ去った列は描かない */
                run_max = (s16)(col_done - (u16)(lo + 56)) < 0 ? RUN_LAG : RUN_MAX;
                cur_col = col_done++;
                if ((u16)(col_done - col_lo) > 64) col_lo = (u16)(col_done - 64);
            } else if ((s16)(col_lo - lo) > 0) {              /* 左へ(逆走): 画面の右の外の列へ前の列 */
                if ((s16)(col_lo - (u16)(lo + 64)) > 0) col_lo = (u16)(lo + 64);
                run_max = (s16)(col_lo - (u16)(lo + 8)) > 0 ? RUN_LAG : RUN_MAX;
                cur_col = --col_lo;
                if ((u16)(col_done - col_lo) > 64) col_done = (u16)(col_lo + 64);
            } else return 0;
            col_b = 0;
        }
        while (col_b < NBLK) {
            s16 t0 = tag[col_b];
            u8 n = 1;
            pcmx_service();                                   /* ★ブロックごとに PCM の送出口(M7) */
            if (col_b == BLK_CONST || t0 < 0 || t0 >= 188 || (u8)(col_b - g_blk_base) < g_blk_skip) { col_b++; continue; }
            while (n < run_max && col_b + n < NBLK && col_b + n != BLK_CONST && tag[col_b + n] == t0 + n * 8) n++;
            cmd_ring((u8)(cur_col & 63), col_b, cur_col, t0, n);
            col_b += n;
            return 1;
        }
    }
}

/* ブロックの描き替え: 都市なら 64 列、高い空なら 2 ページを塗って星 */
static u8 next_job_cmd(void) {
    if (!jn) return 0;
    if (js < 0) {
        u8 row = (u8)(jb0 * 8);
        if (jstep < 2) {
            c_dx = 0; c_dy = (u16)(jstep ? 256u + row : row); c_nx = 256; c_ny = (u16)jn * 8u;
            c_hmmv = 1; c_star = 0;
        } else {
            if (vdp_cmd_busy()) return 0;                     /* ★塗り終わりを待たない(次の呼び出しで打つ。待つと 1 回 10ms 止まった) */
            c_star = jn; c_hmmv = 0; jn = 0;
        }
        jstep++;
        return 1;
    }
    {
        u8 c = jstep;
        u16 wv = win_lo();
        cmd_ring(c, jb0, (u16)(wv + ((u8)(c - (u8)wv) & 63)), js, jn);   /* 窓 [lo, lo + 64) で列 c に入る列 */
        if (++jstep >= 64) jn = 0;
        return 1;
    }
}

static void star(u8 row) {
    u16 x = (u16)rnd8() << 1 | (rnd8() & 1);                 /* 0..511 */
    u8 y = (u8)(row + (rnd8() & 7));
    u8 c = (u8)((rnd8() & 3) == 0 ? 15 : (rnd8() & 1) ? 14 : 9);
    u16 a = (u16)y * 128u + ((x & 255) >> 1);
    u8 v = (x & 1) ? (u8)(0x10 | c) : (u8)((c << 4) | 1);    /* 色 1 の空に 1 点 */
    vdp_write_addr17(0, (u16)((x & 256) ? (a | 0x8000u) : a));
    VDP_DAT = v;
}

/* 7 面の測定空間(g_hall): 星の代わりに方眼。縦の線は 32 ドットおきの点(4 行おき)、横の線は s が 32 の倍数の行。
   ★点 1 つにアドレスの設定が要る(I/O 5 回)。8 行とも打つと 1 ブロック 5ms かかり、上がる間ずっと遅れた */
u8 g_hall;
static void hall_block(u8 row, s16 s0) {
    u8 r;
    for (r = 0; r < 8; r++) {
        u8 y = (u8)(row + r);
        if (((s0 + r) & 31) == 0) { vdp_hmmv(0, y, 256, 1, 0xEE); vdp_hmmv(0, (u16)(256u + y), 256, 1, 0xEE); vdp_cmd_wait(); }
        else if (!((s0 + r) & 3)) {
            u8 k;
            for (k = 0; k < 16; k++) {                        /* x = 32k(0..511) */
                u16 a = (u16)y * 128u + ((k & 7) << 4);
                vdp_write_addr17(0, (u16)((k & 8) ? (a | 0x8000u) : a));
                VDP_DAT = 0xE1;                               /* 左の 1 点が色 14 */
            }
        }
    }
}

/* 8 面の縦穴(g_hall == 2): 高い空のブロックに左右の壁(画面がリングのページ 0 にそろっている前提)。32 行ごとに継ぎ目 */
static void shaft_block(u8 row, s16 s0) {
    vdp_hmmv(0, row, 32, 8, 0xEE); vdp_hmmv(28, row, 4, 8, 0x99);
    vdp_hmmv(224, row, 32, 8, 0xEE); vdp_hmmv(224, row, 4, 8, 0x99);
    if (!(s0 & 31)) { vdp_hmmv(0, row, 32, 1, 0x22); vdp_hmmv(224, row, 32, 1, 0x22); }
    vdp_cmd_wait();
}
/* 高い空のブロックを全部描き直させる(縦穴に入ったとき: 壁のある絵に) */
void stage_sky_redraw(void) {
    u8 b;
    for (b = 0; b < NBLK; b++) if (b != BLK_CONST && tag[b] >= 188 && tag[b] != TAG_NONE) tag[b] = TAG_NONE;
    pick_at = 0x7FFF;
}

/* 欲しい s: lo から 256 行の窓の中で、ブロック b(リングの 8b..8b+7 行)に入る s */
static s16 want_of(u8 b, s16 lo) { return (s16)(lo + (u8)((u8)(b * 8 + 68) - (u8)lo)); }
static s16 tag_of(s16 w) { return w < 0 ? (s16)(w + TAG_SKY) : w; }

/* 描き替えるブロックを探す(進む向きに近いものから)。続くブロックもまとめる */
static void job_pick(void) {
    s16 lo = (s16)(g_scam - (dir < 0 ? LOOK_AHEAD : 8));
    s16 best = 0, w;
    s8 bb = -1;
    u8 b, n, ready = strip_off == STRIP_LEN;
    for (b = 0; b < NBLK; b++) {
        if (b == BLK_CONST) continue;
        w = want_of(b, lo);
        if (w >= 188) continue;                               /* 都市の下(この面では見えない) */
        if ((u8)(b - g_blk_base) < g_blk_skip) { tag[b] = TAG_NONE; continue; }   /* 面が使う行(6 面と終幕の YJK の帯)。★外したら描き直させる
                                                                 (印が古いままだと、7 面の空に YJK のバイトが残った) */
        if (w < 0 ? !upper_sky : !ready) continue;
        if (tag[b] == tag_of(w)) continue;
        if (bb < 0 || (dir >= 0 ? w < best : w > best)) { bb = (s8)b; best = w; }
    }
    if (bb < 0) { pick_at = (s16)(g_scam & ~7); return; }     /* 次は 8 行進んでから探す */
    jb0 = (u8)bb; js = best; jstep = 0; n = 0;
    /* リングで下に続くブロックもまとめる(都市だけ。高い空の HMMV は 1 ブロックで 2KB あり、VDP を長く占める) */
    do {
        tag[jb0 + n] = tag_of((s16)(js + n * 8));            /* 先に印: 描く間に流れてくる列もこのブロックを描く */
        n++;
    } while (js >= 0 && n < RUN_MAX && jb0 + n < NBLK && jb0 + n != BLK_CONST &&
             (w = want_of((u8)(jb0 + n), lo)) == js + n * 8 && w < 188 && (w < 0 ? upper_sky : ready) &&
             tag[jb0 + n] != tag_of(w));
    jn = n;
}

/* budget = 待ってよい時間(0 = VDP が空いている間だけ出して、待たない) */
static u8 stage_work(void);
void stage_pump(u16 budget) {
    u16 t0;
    if (!stage_work()) return;
    t0 = sys_timer();
    for (;;) {
        /* ★VDP が動いている間(GHOST の大きな HMMM など)は、予算の間だけ待つ。vdp_hmmm に待たせると
           PLUMB の 64x64 の写し(約 12ms)を丸ごと待った */
        if (vdp_cmd_busy()) {
            if ((u16)(sys_timer() - t0) >= budget) return;
            continue;
        }
        if (!jn && pick_at != (s16)(g_scam & ~7)) job_pick();
        if (!next_job_cmd() && (strip_off != STRIP_LEN || !next_col_cmd())) return;   /* 列は短冊が揃ってから */
        if (c_star) {
            u8 i;
            vdp_cmd_wait();                                   /* 塗り終わってから */
            for (i = 0; i < c_star; i++) {
                if (g_hall == 2) shaft_block((u8)((jb0 + i) * 8), (s16)(js + i * 8));
                else if (g_hall) hall_block((u8)((jb0 + i) * 8), (s16)(js + i * 8));
                else { star((u8)((jb0 + i) * 8)); star((u8)((jb0 + i) * 8)); star((u8)((jb0 + i) * 8)); }
            }
        } else if (c_hmmv) vdp_hmmv(c_dx, c_dy, c_nx, c_ny, 0x11);
        else vdp_hmmm(c_sx, c_sy, c_dx, c_dy, c_nx, c_ny);
        pcmx_service();                                       /* ★コマンドごとに PCM の送出口(M7) */
        if ((u16)(sys_timer() - t0) > PUMP_BUDGET) return;
    }
}

/* 列と描き替えの仕事が残っているか(VDP の状態を読む前に C だけで調べる) */
/* ★短冊が無い・読んでいる間も、高い空のブロックの描き替え(job)は回す(7 面・終幕で空が描き替わらなかった) */
static u8 stage_work(void) {
    u16 lo = win_lo();
    if (jn || pick_at != (s16)(g_scam & ~7)) return 1;
    if (strip_off != STRIP_LEN) return 0;
    return (u8)(col_b < NBLK || redraw_n || (s16)(col_done - (u16)(lo + 64)) < 0 || (s16)(col_lo - lo) > 0);
}
u8 stage_busy(void) { return stage_work(); }
/* 世界の列 wcol が描き終わっていて、描きかけでもない(その上に BG を写してよい) */
u8 stage_col_ready(u16 wcol) {
    if ((u16)(wcol - col_lo) >= (u16)(col_done - col_lo)) return 0;
    return (u8)(col_b >= NBLK || cur_col != wcol);
}
u8 g_bands_all;
u8 g_blk_skip, g_blk_base;
u8 g_start_stage;         /* ブロック g_blk_base から g_blk_skip 個(リングの 8 行ずつ)を描かない */


/* メインループの途中で(待たない): 列と描き替え、GHOST の待ち行列。
   ★仕事が無ければ VDP を読まずに戻る(CE を見るだけで I/O 5 回 ≒ 0.7 ライン。1 フレームに 3 回呼ぶ) */
void stage_pump_nb(void) {
    if (g_split_shown) return;                                /* ★上の帯が SCREEN12 の間は VRAM に触らない(窓は RAS_MARK の後) */
    if ((strip_off == STRIP_LEN || (strip_off == 0xFFFF && upper_sky != UP_AIR)) && stage_work()) stage_pump(0);   /* 短冊を読む間は VRAM の窓の 1 回だけ */
    if (gq_free() < GQ_N) gq_pump(0);
}

/* ---- 面の開始表示(指示書 4.2): HUD 帯の上に 16 行の帯を出し、「STAGE 2 / GRAVITY WELL」を左から滑り込ませ、
   2 秒で HUD 帯へ吸い込ませる(帯が上から縮み、文字が上から消える)。
   ★帯はページ 2|3 の (行 + R#23) の行を見せる。R#23 はプレイエリアと同じ値のまま(変えるとその帯のスプライトの
     Y がずれる)なので、2 面の空中戦(g_scam = -192、R#23 = 252)では画面の 172..187 行 = 168..183 行。
     ページ 2 のそこは起動ロゴの黒い区間、ページ 3 は都市の短冊(もう使わない)なので、色 1 で塗って使い、
     終わったらページ 2 を黒へ戻す。横は 256(ページ 3 = 何もない)から 0 へ寄せて、文字を左から入れる。
     帯の色 1 はその高さの空の色にする(透けて見える)。
   ★面の短冊を使っている間(読み込み中も)はページ 3 を塗らず、文字を滑り込ませずにその場に出す(ページ 3 の 160..183 行は
     短冊の地面。デモの説明の帯がそこを塗り、以後に流れてくる 1 面の列の街路が空の色になって、Pawn の黒い余白が四角く
     見えた)。 */
#define BAN_LINE  172
#define BAN_ROW   168              /* (BAN_LINE + 252) & 255 */
#define BAN_IN    7                /* ここから見せる(3..6 は文字を書く) */
#define BAN_SUCK  96               /* ここから吸い込む */
#define BAN_END   (BAN_SUCK + 22)       /* 吸い込み: 帯の上端を 2 フレームに 1 行下げる(HUD の手前 6 行まで) */
static u8 ban_regs[4];
static u16 ban_x;
static u8 ban_p3;                  /* 1 = ページ 3 を塗って文字を滑り込ませる(短冊を使っていないとき) */

static void banner_vram(void) {
    if (banner_t <= 3 && vdp_cmd_busy()) return;            /* 塗りは VDP が空いてから(待つと遅れた。3 面の入り) */
    if (banner_t == 1) { vdp_hmmv(0, 512 + BAN_ROW - 8, 256, 24, 0x11); ban_p3 = strip_off == 0xFFFF; }   /* 1 フレームに 1 つ(VDP を待たない) */
    else if (banner_t == 2) { if (ban_p3) vdp_hmmv(0, 768 + BAN_ROW - 8, 256, 24, 0x11); }
    else if (banner_t >= 3 && banner_t < BAN_IN) {           /* 文字は 1 フレームに 2 行ずつ(一度に書くと 120 ライン。2 面の入りで遅れた) */
        u8 r = (u8)((banner_t - 3) << 1);
        extern u16 g_frame_t0;
        if ((u16)(sys_timer() - g_frame_t0) > 150u * 16u) return;   /* ★重いフレームは次へ(2 行で約 50 ライン。7 面の試験の
                                                                   切り替えで描き替えの仕事と重なって遅れた) */
        vdp_cmd_wait();
        font_text_row(2, 40, (u8)(BAN_ROW + 4), banner_fg, 1, banner_text, r);
        font_text_row(2, 40, (u8)(BAN_ROW + 4), banner_fg, 1, banner_text, (u8)(r + 1));
        ban_x = ban_p3 ? 256 : 0;
    } else if (banner_t == BAN_END) {
        vdp_hmmv(0, 512 + BAN_ROW - 8, 256, 24, 0x00);       /* 起動ロゴの黒い区間へ戻す */
        banner_t = 0;
        return;
    }
    if (banner_t >= BAN_IN && ban_x) ban_x -= (ban_x + 7) >> 3;   /* 左から寄ってきて止まる */
    banner_t++;
}

void stage_vram(void) {
    if (banner_t) banner_vram();
    pcmx_service();
    if (ovl_step()) return;                                   /* オーバレイの読み込み(512 バイト/フレーム)。★ovl_init のフレームは
                                                                 ほかの仕事をしない(地図の読み込み + 短冊 + 面の素材で遅れた) */
    if (g_ovl_ready && g_ovl >= OVL_S1) ovl_vram();
    if (strip_off != STRIP_LEN) {                             /* 地面がまだ無い(0xFFFF)か、短冊を読んでいる */
        extern u16 g_frame_t0;
        if (strip_off < STRIP_LEN && (u16)(sys_timer() - g_frame_t0) < 160u * 16u) {   /* 空中戦の間に短冊をページ 3 へ(256 バイト/フレーム。重いフレームは次へ) */
            rom_to_vram(strip_bank, (u16)(strip_src + strip_off), 1, (u16)(0x8000u + strip_off), 256);
            strip_off += 256;
            if (strip_off >= STRIP_LEN) {                     /* 以降の列は流しながら */
                strip_off = STRIP_LEN; col_lo = win_lo(); col_done = (u16)(col_lo + 64); pick_at = 0x7FFF;
            }
        }
        /* ★流す列の仕事は短冊が揃ってから(col_done = 0 から何百列も空回りした)。高い空の描き替えだけは回す
           (7 面・終幕は短冊を読まない。1 面の空中戦(UP_AIR)は空を描き替えないので呼ばない: 呼ぶだけで
           フレームの余りが減り、処理落ちのフレームが替わった) */
        if (upper_sky != UP_AIR && stage_work()) stage_pump(0);
        return;
    }
    pcmx_service();
    {   /* 予算はフレームの残りから(VRAM の仕事の後はリストを組むだけ。ここで 200 ラインを超えたら出さない) */
        extern u16 g_frame_t0;
        u16 used = (u16)(sys_timer() - g_frame_t0), lim = 200u * 16u;
        stage_pump(used >= lim ? 0 : (lim - used < PUMP_BUDGET ? (u16)(lim - used) : PUMP_BUDGET));
    }
}

/* ---- ラスターリスト ----
   帯の境目は s の順 = 画面の行の順に出てくるので、並べ替えずにそのまま項目へ詰める。
   0 行以前の境目は 0 行の項目に上書きでまとめる。それ以外は RAS_GAP 未満の境目を同じ項目へ(同じ種類が
   もうあれば後ろへずらす)。96 行の属性表の切り替えは、その行を越える境目の前に差し込む。 */
static RasEntry *E;
static u8 K, sat_done, *sat_regs;

static RasEntry *slot(s16 line, u8 kind);
static void sat_entry(void) {
    RasEntry *c;
    sat_done = 1;
    c = slot(96, RAS_REGS);
    if (c) { c->haze = sat_regs; c->haze_n = 3; }
}
static RasEntry *slot(s16 line, u8 kind) {
    RasEntry *c;
    if (line < 0) line = 0;
    if (line >= 96 && !sat_done && kind != RAS_REGS) sat_entry();
    c = &E[K - 1];
    if (line == 0 ? c->line == 0 : line < (s16)c->line + RAS_GAP) {
        if (line == 0 || !(c->flags & kind)) { c->flags |= kind; return c; }
        line = (s16)(c->line + RAS_GAP);
    }
    if (line >= ITEM_END || K >= RAS_MAX - 1) return 0;     /* HUD の項目(HUD_IRQ)の手前はあける */
    c = &E[K++];
    c->line = (u8)line; c->flags = kind;
    return c;
}
/* 並びを調べずに項目を足す(0 行の項目のすぐ後に、帰線の 244 行の項目などを置くため) */
RasEntry *stage_ev_raw(u8 line, u8 flags) {
    RasEntry *c;
    if (K >= RAS_MAX - 1) return 0;
    c = &E[K++];
    c->line = line; c->flags = flags;
    return c;
}
void stage_ev_scroll(s16 line, u16 x) {
    RasEntry *c = slot(line, RAS_SCROLL);
    if (c) { c->r26 = VDP_R26(x); c->r27 = VDP_R27(x); }
}
/* HR ポーリング帯(R#27 だけ毎ライン。tbl は (r26, r27) の組 × n)。前の項目からは RAS_GAP あける */
void stage_ev_haze(s16 line, u8 n, u8 *tbl) {
    RasEntry *c;
    if (line < 0) line = 0;
    c = &E[K - 1];
    if (line < (s16)c->line + RAS_GAP) line = (s16)(c->line + RAS_GAP);
    {   /* ★下は切り詰める(HUD の項目の手前まで) */
        s16 m = (s16)(ITEM_END - 1 - line);
        if (m <= 0 || K >= RAS_MAX - 1) return;
        if (n > m) n = (u8)m;
    }
    c = &E[K++];
    c->line = (u8)line; c->flags = RAS_HAZE | RAS_HAZE_FINE; c->haze = tbl; c->haze_n = n;
}

/* 帯の縦スクロール(R#23)。ISR は R#23 の書き込みを覚えて以降の R#19 に足す。★スプライトもその帯では R#23 の差だけ
   ずれて見えるので、自機の入れない帯で使う(5 面のプレス)。1 フレーム VS_MAX 個まで */
#define VS_MAX 16                  /* 8 面の歪曲回廊は帯ごとに R#23 を替える */
static u8 vs_regs[2][VS_MAX][2], vs_n, vs_bi;
void stage_ev_vscroll(s16 line, u8 r23) {
    RasEntry *c;
    if (vs_n >= VS_MAX || line <= 0) return;
    if (line >= 96 && !sat_done) sat_entry();                 /* 属性表の切り替え(RAS_REGS)より先に並べない */
    c = slot(line, RAS_REGS);                                 /* slot は RAS_REGS を持つ項目へは混ぜない(次の行へずらす) */
    if (!c || (c->flags & RAS_HAZE)) return;                  /* HR ポーリングの項目とは表(haze)を共有できない */
    vs_regs[vs_bi][vs_n][0] = 23; vs_regs[vs_bi][vs_n][1] = r23;
    c->haze = vs_regs[vs_bi][vs_n]; c->haze_n = 1;
    vs_n++;
}

void stage_ev_pal_i(s16 line, u8 idx, const u8 *rgb) {
    RasEntry *c = slot(line, RAS_PAL);
    if (c) { c->pidx = idx; c->prb = (u8)((rgb[0] << 4) | rgb[2]); c->pg = rgb[1]; }
}
void stage_ev_pal(s16 line, const u8 *rgb) { stage_ev_pal_i(line, 1, rgb); }
/* 色 1 と横スクロールを 1 つの項目に(slot を 1 回で済ませる。雲の層を 12 帯並べる 4 面で、帯ごとに pal と scroll を別に
   呼ぶと項目を組むだけで 1 フレーム 1ms 近く使い、VERNIER の間は空きが 0 になった。M7 の後) */
void stage_ev_ps(s16 line, const u8 *rgb, u16 x) {
    RasEntry *c = slot(line, RAS_PAL | RAS_SCROLL);
    if (c) {
        c->pidx = 1; c->prb = (u8)((rgb[0] << 4) | rgb[2]); c->pg = rgb[1];
        c->r26 = VDP_R26(x); c->r27 = VDP_R27(x);
    }
}

static const u8 hs_indigo[8][3] = { { 0, 0, 2 }, { 1, 0, 2 }, { 1, 0, 3 }, { 1, 1, 3 }, { 2, 1, 3 }, { 2, 1, 4 }, { 3, 1, 4 }, { 3, 2, 4 } };
static const u8 hs_black[8][3]  = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 1, 0, 1 }, { 1, 0, 2 } };
static u8 hs_cur[8][3];
static const u8 sky_line[7] = { 0, 16, 32, 48, 64, 80, 96 };
static u8 regs_top[2][16], regs_b[2][8];

/* R#1 の組(RG1SAV に MAG を足す)。★stage_build に直に書くと SDCC の割り当てが崩れて 150 バイト太った */
void mag_pair(u8 *p, u8 m) { p[0] = 1; p[1] = (u8)(*(volatile u8 *)0xF3E0 | m); }

void stage_build(void) {
    u8 bi = ras_back_index(), i;
    u8 *rt = regs_top[bi];
    s16 sc = g_scam;
    u16 nx = bg_layer_x(2);

    E = ras_back(); K = 1; sat_done = 0; vs_n = 0; vs_bi = bi;
    sat_regs = regs_b[bi];
    sat_regs[0] = 5; sat_regs[1] = spr_regs.r5b; sat_regs[2] = 11; sat_regs[3] = spr_regs.r11b;
    rt[0] = 5; rt[1] = spr_regs.r5a; rt[2] = 11; rt[3] = spr_regs.r11a;
    rt[4] = 2; rt[5] = 0x3F; rt[6] = 23; rt[7] = (u8)(g_vscroll + g_roll);
    /* ★部分拡大(MAG = R#1 bit0、M7 の後): 帯の境(96 行)で R#1 を替え、片方の帯だけスプライトを 2 倍にする。
       境をまたぐスプライトは裂けるので、自機のいない帯だけ(main.c の撃破の演出) */
    mag_pair(rt + 8, g_mag & 1); mag_pair(sat_regs + 4, g_mag >> 1);
    E[0].line = 0; E[0].flags = RAS_REGS; E[0].haze = rt; E[0].haze_n = 5;

    if (g_bands_all && g_ovl_ready && g_ovl >= OVL_S1) {     /* 面が帯を全部出す(空の上にも帯を置く面。5 面の GAUGE BLOCK) */
        ovl_bands(sc, nx);
        goto tail;
    }
    /* s < 0 */
    if (upper_sky == UP_AIR) {
        for (i = 0; i < 7; i++) stage_ev_pal((s16)(S_AIR + sky_line[i] - sc), bg_sky(i));
        stage_ev_scroll((s16)(-88 - sc), bg_layer_x(0));
        stage_ev_scroll((s16)(-64 - sc), bg_layer_x(1));
        stage_ev_scroll((s16)(-38 - sc), nx);
    } else {
        if (hs_t != (u8)(g_sky_t | (g_hall << 7))) {
            u8 j;
            hs_t = (u8)(g_sky_t | (g_hall << 7));
            for (i = 0; i < 8; i++)
                for (j = 0; j < 3; j++)
                    hs_cur[i][j] = g_hall == 1 ? (u8)(5 + (i >> 2) + (j == 2)) :   /* 測定空間: 白(下ほど明るい) */
                                   (u8)((hs_indigo[i][j] * (16u - g_sky_t) + hs_black[i][j] * g_sky_t + 8u) >> 4);
        }
        for (i = 0; i < 8; i++) {
            s16 l = (s16)(S_AIR2 + i * 24 - sc);
            if (l + 24 <= 0 && i < 7) continue;                       /* 画面の上の外で終わる帯は出さない(0 行へまとまるだけ) */
            stage_ev_pal(l, hs_cur[i]);
            if (i == 0) stage_ev_scroll(l, nx >> 2);                  /* 星の 3 層: 遠い・中・近い */
            else if (i == 3) stage_ev_scroll(l, nx >> 1);
            else if (i == 6) stage_ev_scroll(l, nx);
        }
    }
    /* s >= 0: 地面(面のオーバレイが帯を出す) */
    stage_ev_scroll((s16)(0 - sc), nx);
    if (g_ovl_ready && g_ovl >= OVL_S1) ovl_bands(sc, nx);
tail:
    if (!sat_done) sat_entry();
    if (banner_t >= BAN_IN && banner_t < BAN_END) {
        s16 line = (s16)(BAN_LINE + (banner_t > BAN_SUCK ? (banner_t - BAN_SUCK) >> 1 : 0));
        RasEntry *c = &E[K - 1];
        if (line < (s16)c->line + RAS_GAP) line = (s16)(c->line + RAS_GAP);
        if (line < ITEM_END && K < RAS_MAX - 1) {
            c = &E[K++];
            /* ページ 2|3。★R#23 も 252(s = -192 のときの値)にする: プレイエリアの R#23 のままだと、カメラが宇宙の
               高さにいない面の途中(7 面の判定文など)で帯がページ 2 の別の行(ボスの絵)を見せた */
            ban_regs[0] = 2; ban_regs[1] = 0x7F; ban_regs[2] = 23; ban_regs[3] = (u8)(BAN_ROW - BAN_LINE);
            c->line = (u8)line; c->flags = RAS_REGS | RAS_SCROLL | RAS_PAL;
            c->haze = ban_regs; c->haze_n = 2;
            c->r26 = VDP_R26(ban_x); c->r27 = VDP_R27(ban_x);
            c->pidx = 1; c->prb = (u8)((hs_cur[7][0] << 4) | hs_cur[7][2]); c->pg = hs_cur[7][1];
        }
    }

    bg_fill_hud(&E[K], bi); K++;
    ras_commit(K);
    g_split_shown = g_split_n;                                 /* 6 面の上の帯(SCREEN12)。VRAM の窓は RAS_MARK の後 */
}
