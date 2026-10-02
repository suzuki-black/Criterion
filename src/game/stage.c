/* stage.c — 1 面の進行と、縦に長い世界の表示(指示書 5.1)。
   空中戦 → 降下(R#23 の縦スクロールと横スクロールを同時に)→ 都市前半 → 中ボス → 都市後半 → ボス →
   上昇(縦スクロールで空へ抜ける)→ 2 面の空中戦。画面は一度も消さない。

   縦の座標 s(game.h): 空中戦の空と雲が s = -188..-1、都市が s = 0..187(雲の底 / 空とビル / 歩道 / 街路)。
   表示リング(ページ 0|1、512x256)の行 = (188 + s) & 255。画面には 188 行しか出ないので、リングの残りの
   68 行と、画面から出た行を、次に見える s の内容へ描き替えながら進む。
   ・8 行ごとのブロック(32 個)に「今どの s を持っているか」(tag)を持つ。カメラの窓(進む向きに 60 行先まで)
     で欲しい s と違い、かつ画面に出ていないブロックを描き替える。
       都市(s >= 0): ページ 3 の短冊(8x188、tools/gen_city.py)から 1 列ずつ HMMM。1 ブロック = 64 列。
       高い空(上昇から。s < 0): HMMV で空の色(色 1)を塗り、星を打つ。
       空中戦の空と雲(s < 0): 起動時に描いたもの(bg.c)をそのまま使う(描き直さない)。
   ・横は 8 ドット(1 列)進むごとに、画面の右の外の列を地図の次の列で描く(都市のブロックだけ)。
   ・★ブロック 23(リングの 184..191 行)は s = -4..3。空中戦の近い雲の底も都市の雲の底の上端も色 6 だけ
     なので、描き替えない。
   ・ラスターは s の帯の境目を「画面の行 = s - g_scam」に置く。画面より上に出た境目は 0 行の項目にまとめる。
     項目どうしは RAS_GAP 行以上あける(近い境目は 1 つの項目にまとめるか、後ろへずらす)。 */
#include "game.h"
#include "vdp.h"
#include "raster.h"
#include "spr.h"
#include "rom.h"
#include "assets.h"
#include "city_data.h"
#include "sys.h"
#include "font.h"

#define AIR1_LEN   512             /* 空中戦(ドット) */
#define DESC_LEN   256             /* 降下(この間に g_scam が -188 → 0) */
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

static s16 scam_q;                 /* g_scam × 16 */
static s8  dir;                    /* カメラの縦の向き(+1 = 降下、-1 = 上昇) */
static u16 sec_wx;                 /* 区間に入ったときの g_wx */
static u16 sec_t;                  /* 区間に入ってからのフレーム */
static u16 w0;                     /* 地図の列 0 の世界の列(8 ドット単位) */
static u8  upper_sky;              /* 1 = s < 0 は高い空(上昇から) */
static s16 tag[NBLK];
static u8  cmap[CITY_COLS];
static u16 strip_off;              /* 短冊を ROM から読んだバイト数 */
static u16 col_done;               /* 描いた最後の世界の列 + 1 */
static u8  turret_i;
static u16 cur_col;                /* 流している列 */
static u8  col_b;                  /* その列の次のブロック(NBLK = 列を終えた) */
static u8  jb0, jn, jstep;         /* 描き替え中のブロック(先頭・個数。jn = 0 で仕事なし)と進み */
static s16 js;
static s16 pick_at;                /* 前に描き替えを探して何も無かったときの g_scam(8 行単位) */
static u8  hs_t;                   /* hs_cur を求めたときの sky_t */
static u8  banner_t;               /* 面の開始表示の進み(0 = なし) */
static u8  sky_t;                  /* 高い空の暗さ(0 = 藍 .. 16 = 黒) */

/* ---- 区間 ---- */
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
    if (sec == SEC_ASCENT) { upper_sky = 1; g_scroll_speed = 8; pick_at = 0x7FFF; }
    if (sec == SEC_AIR2) { g_scroll_speed = 4; banner_t = 1; }
}

void stage_start(void) {
    u8 b;
    g_wx = bg_layer_x(2);
    w0 = (u16)((g_wx + AIR1_LEN) >> 3);
    scam_q = S_AIR * 16; g_scam = S_AIR; dir = 1;
    upper_sky = 0; sky_t = 0;
    for (b = 0; b < NBLK; b++) tag[b] = b < BLK_CONST ? (s16)(b * 8 - 188) : TAG_NONE;
    tag[BLK_CONST] = -4;
    rom_to_ram(ASSET_CITY_MAP_BANK, ASSET_CITY_MAP_OFF, cmap, CITY_COLS);
    strip_off = 0; col_done = 0; col_b = NBLK; jn = 0; pick_at = 0x7FFF; turret_i = 0; hs_t = 0xFF; banner_t = 0;
    enter(SEC_AIR1);
}

static u16 col_of_screen(void) { return (u16)((g_wx >> 3) - w0); }   /* 画面の左端の地図の列 */

void stage_update(void) {
    s16 target = g_scam;
    g_wx = bg_layer_x(2);
    sec_t++;
    switch (g_sec) {
    case SEC_AIR1:
        if ((u16)(g_wx - sec_wx) >= AIR1_LEN) enter(SEC_DESCENT);
        break;
    case SEC_DESCENT: {
        u16 d = (u16)(g_wx - sec_wx);
        if (d >= DESC_LEN) { scam_q = 0; enter(SEC_CITY1); }
        else scam_q = (s16)(S_AIR * 16 + (s16)((d * 47u) >> 2));   /* 188 行 / 256 ドット = 47/64(× 16) */
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
        scam_q -= 24;                                   /* 1.5 行/フレーム */
        if (sky_t < 16 && !(sec_t & 7)) sky_t++;
        if (scam_q <= S_AIR2 * 16) { scam_q = S_AIR2 * 16; enter(SEC_AIR2); }
        break;
    default:
        break;
    }
    g_scam = (s16)(scam_q >> 4);
    if (g_scam != target) dir = g_scam > target ? 1 : -1;
    g_vscroll = (u8)(188 + g_scam);

    /* 屋上の砲台: 列が画面の右端に来たら置く */
    while (turret_i < CITY_NTURRET) {
        u16 tx = (u16)((w0 + city_turret_col[turret_i]) * 8u + 4u);
        if ((s16)(tx - g_wx) > 264) break;
        if ((s16)(tx - g_wx) > 0) enemy_spawn_spirit(tx, city_turret_row[turret_i]);
        turret_i++;
    }
}

/* ---- 表示リングへの描き込み ----
   ★VDP のコマンド(HMMM は 1 バイト約 5us)を待つと CPU が止まるので、仕事は「次の 1 コマンド」ずつに分け、
     pump が時間の予算(PUMP_BUDGET)の間だけ続けて出す。最後に出したコマンドは次のフレームまでに VDP が
     終える(CPU は待たない)。1 コマンドは最大 3 ブロック(8x24 = 96 バイト ≒ 0.5ms)。 */
#define PUMP_BUDGET 330            /* S1990 タイマのカウント(≒ 20 ライン) */
#define RUN_MAX     3

static u8 strip_of(u16 wcol) {
    u16 m = (u16)(wcol - w0);
    if (wcol < w0 || m >= CITY_COLS) return 0;
    return cmap[m];
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
            if (col_done >= (u16)((g_wx >> 3) + 64)) return 0;
            cur_col = col_done++; col_b = 0;
        }
        while (col_b < NBLK) {
            s16 t0 = tag[col_b];
            u8 n = 1;
            if (col_b == BLK_CONST || t0 < 0 || t0 >= 188) { col_b++; continue; }
            while (n < RUN_MAX && col_b + n < NBLK && col_b + n != BLK_CONST && tag[col_b + n] == t0 + n * 8) n++;
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
        } else { c_star = jn; c_hmmv = 0; jn = 0; }
        jstep++;
        return 1;
    }
    {
        u8 c = jstep;
        u16 wv = g_wx >> 3;
        cmd_ring(c, jb0, (u16)(wv + ((u8)(c - (u8)wv) & 63)), js, jn);   /* 窓 [wv, wv + 64) で列 c に入る列 */
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

/* 欲しい s: lo から 256 行の窓の中で、ブロック b(リングの 8b..8b+7 行)に入る s */
static s16 want_of(u8 b, s16 lo) { return (s16)(lo + (u8)((u8)(b * 8 + 68) - (u8)lo)); }
static s16 tag_of(s16 w) { return w < 0 ? (s16)(w + TAG_SKY) : w; }

/* 描き替えるブロックを探す(進む向きに近いものから)。続くブロックもまとめる */
static void job_pick(void) {
    s16 lo = (s16)(g_scam - (dir < 0 ? LOOK_AHEAD : 8));
    s16 best = 0, w;
    s8 bb = -1;
    u8 b, n, ready = strip_off >= STRIP_LEN;
    for (b = 0; b < NBLK; b++) {
        if (b == BLK_CONST) continue;
        w = want_of(b, lo);
        if (w >= 188) continue;                               /* 都市の下(この面では見えない) */
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

static void pump(void) {
    u16 t0 = sys_timer();
    for (;;) {
        /* ★VDP が動いている間(GHOST の大きな HMMM など)は、予算の間だけ待つ。vdp_hmmm に待たせると
           PLUMB の 64x64 の写し(約 12ms)を丸ごと待った */
        if (vdp_cmd_busy()) {
            if ((u16)(sys_timer() - t0) > PUMP_BUDGET) return;
            continue;
        }
        if (!jn && pick_at != (s16)(g_scam & ~7)) job_pick();
        if (!next_job_cmd() && !next_col_cmd()) return;
        if (c_star) {
            u8 i;
            vdp_cmd_wait();                                   /* 塗り終わってから */
            for (i = 0; i < c_star; i++) { star((u8)((jb0 + i) * 8)); star((u8)((jb0 + i) * 8)); star((u8)((jb0 + i) * 8)); }
        } else if (c_hmmv) vdp_hmmv(c_dx, c_dy, c_nx, c_ny, 0x11);
        else vdp_hmmm(c_sx, c_sy, c_dx, c_dy, c_nx, c_ny);
        if ((u16)(sys_timer() - t0) > PUMP_BUDGET) return;
    }
}

/* ---- 面の開始表示(指示書 4.2): HUD 帯の上に 16 行の帯を出し、「STAGE 2 / GRAVITY WELL」を左から滑り込ませ、
   2 秒で HUD 帯へ吸い込ませる(帯が上から縮み、文字が上から消える)。
   ★帯はページ 2|3 の (行 + R#23) の行を見せる。R#23 はプレイエリアと同じ値のまま(変えるとその帯のスプライトの
     Y がずれる)なので、2 面の空中戦(g_scam = -192、R#23 = 252)では画面の 172..187 行 = 168..183 行。
     ページ 2 のそこは起動ロゴの黒い区間、ページ 3 は都市の短冊(もう使わない)なので、色 1 で塗って使い、
     終わったらページ 2 を黒へ戻す。横は 256(ページ 3 = 何もない)から 0 へ寄せて、文字を左から入れる。
     帯の色 1 はその高さの空の色にする(透けて見える)。 */
#define BAN_LINE  172
#define BAN_ROW   168              /* (BAN_LINE + 252) & 255 */
#define BAN_IN    4                /* ここから見せる */
#define BAN_SUCK  96               /* ここから吸い込む */
#define BAN_END   (BAN_SUCK + 22)       /* 吸い込み: 帯の上端を 2 フレームに 1 行下げる(HUD の手前 6 行まで) */
static u8 ban_regs[2];
static u16 ban_x;

static void banner_vram(void) {
    if (banner_t == 1) vdp_hmmv(0, 512 + BAN_ROW - 8, 256, 24, 0x11);      /* 1 フレームに 1 つ(VDP を待たない) */
    else if (banner_t == 2) vdp_hmmv(0, 768 + BAN_ROW - 8, 256, 24, 0x11);
    else if (banner_t == 3) {
        vdp_cmd_wait();
        font_text(2, 40, (u8)(BAN_ROW + 4), 15, 1, "STAGE 2 / GRAVITY WELL");
        ban_x = 256;
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
    boss_vram();
    if (strip_off < STRIP_LEN) {                              /* 空中戦の間に短冊をページ 3 へ(256 バイト/フレーム) */
        rom_to_vram(ASSET_CITY_STRIPS_BANK, (u16)(ASSET_CITY_STRIPS_OFF + strip_off), 1,
                    (u16)(0x8000u + strip_off), 256);
        strip_off += 256;
        if (strip_off >= STRIP_LEN) { col_done = (u16)((g_wx >> 3) + 64); pick_at = 0x7FFF; }   /* 以降の列は流しながら */
        return;
    }
    pump();
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
    if (c) { c->haze = sat_regs; c->haze_n = 2; }
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
    if (line >= PLAY_H - RAS_GAP || K >= RAS_MAX - 1) return 0;   /* HUD の項目(188 行)の手前はあける */
    c = &E[K++];
    c->line = (u8)line; c->flags = kind;
    return c;
}
static void ev_scroll(s16 line, u16 x) {
    RasEntry *c = slot(line, RAS_SCROLL);
    if (c) { c->r26 = VDP_R26(x); c->r27 = VDP_R27(x); }
}
static void ev_pal(s16 line, const u8 *rgb) {
    RasEntry *c = slot(line, RAS_PAL);
    if (c) { c->pidx = 1; c->prb = (u8)((rgb[0] << 4) | rgb[2]); c->pg = rgb[1]; }
}

static const u8 city_sky[6][3] = { { 2, 1, 2 }, { 3, 1, 3 }, { 4, 1, 3 }, { 5, 2, 3 }, { 6, 3, 3 }, { 7, 4, 2 } };
static const u8 city_sky_s[6] = { 0, 14, 40, 70, 100, 130 };
static const u8 hs_indigo[8][3] = { { 0, 0, 2 }, { 1, 0, 2 }, { 1, 0, 3 }, { 1, 1, 3 }, { 2, 1, 3 }, { 2, 1, 4 }, { 3, 1, 4 }, { 3, 2, 4 } };
static const u8 hs_black[8][3]  = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 1, 0, 1 }, { 1, 0, 2 } };
static u8 hs_cur[8][3];
static const u8 sky_line[7] = { 0, 16, 32, 48, 64, 80, 96 };
static u8 regs_top[2][8], regs_b[2][4];

void stage_build(void) {
    u8 bi = ras_back_index(), i;
    u8 *rt = regs_top[bi];
    s16 sc = g_scam;
    u16 nx = bg_layer_x(2);

    E = ras_back(); K = 1; sat_done = 0;
    sat_regs = regs_b[bi];
    sat_regs[0] = 5; sat_regs[1] = spr_regs.r5b; sat_regs[2] = 11; sat_regs[3] = spr_regs.r11b;
    rt[0] = 5; rt[1] = spr_regs.r5a; rt[2] = 11; rt[3] = spr_regs.r11a;
    rt[4] = 2; rt[5] = 0x3F; rt[6] = 23; rt[7] = g_vscroll;
    E[0].line = 0; E[0].flags = RAS_REGS; E[0].haze = rt; E[0].haze_n = 4;

    /* s < 0 */
    if (!upper_sky) {
        for (i = 0; i < 7; i++) ev_pal((s16)(S_AIR + sky_line[i] - sc), bg_sky(i));
        ev_scroll((s16)(-88 - sc), bg_layer_x(0));
        ev_scroll((s16)(-64 - sc), bg_layer_x(1));
        ev_scroll((s16)(-38 - sc), nx);
    } else {
        if (hs_t != sky_t) {
            u8 j;
            hs_t = sky_t;
            for (i = 0; i < 8; i++)
                for (j = 0; j < 3; j++)
                    hs_cur[i][j] = (u8)((hs_indigo[i][j] * (16u - sky_t) + hs_black[i][j] * sky_t + 8u) >> 4);
        }
        for (i = 0; i < 8; i++) {
            s16 l = (s16)(S_AIR2 + i * 24 - sc);
            ev_pal(l, hs_cur[i]);
            if (i == 0) ev_scroll(l, nx >> 2);                  /* 星の 3 層: 遠い・中・近い */
            else if (i == 3) ev_scroll(l, nx >> 1);
            else if (i == 6) ev_scroll(l, nx);
        }
    }
    /* s >= 0: 都市 */
    ev_scroll((s16)(0 - sc), nx);
    for (i = 0; i < 6; i++) ev_pal((s16)(city_sky_s[i] - sc), city_sky[i]);
    if (!sat_done) sat_entry();
    if (banner_t >= BAN_IN && banner_t < BAN_END) {
        s16 line = (s16)(BAN_LINE + (banner_t > BAN_SUCK ? (banner_t - BAN_SUCK) >> 1 : 0));
        RasEntry *c = &E[K - 1];
        if (line < (s16)c->line + RAS_GAP) line = (s16)(c->line + RAS_GAP);
        if (line <= PLAY_H - RAS_GAP && K < RAS_MAX - 1) {
            c = &E[K++];
            ban_regs[0] = 2; ban_regs[1] = 0x7F;                  /* ページ 2|3 */
            c->line = (u8)line; c->flags = RAS_REGS | RAS_SCROLL | RAS_PAL;
            c->haze = ban_regs; c->haze_n = 1;
            c->r26 = VDP_R26(ban_x); c->r27 = VDP_R27(ban_x);
            c->pidx = 1; c->prb = (u8)((hs_cur[7][0] << 4) | hs_cur[7][2]); c->pg = hs_cur[7][1];
        }
    }

    bg_fill_hud(&E[K], bi); K++;
    ras_commit(K);
}
