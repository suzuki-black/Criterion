/* restore.c — 画面の矩形を背景の絵へ描き戻す(GHOST の待ち行列。2026-10-29、1 面の Pawn がビルの壁を登るため)。
   表示リングの x は世界の x と 512 で同じなので列は (g_wx + x) >> 3、リングの行のブロック(8 行)に描いてある s は
   stage.c の g_blk_tag。そこからページ 3 の短冊(遠景は far の短冊。stage_src_of)を写す。短冊の外(空・共通のブロック)は色 1 で塗る。
   8 ドットの列 × ブロックごとに 1 コマンド(小さな物を消す用。x と w は偶数、画面の中で)。
   ★大きさ優先でコンパイルする(Makefile の SIZE_FIRST。速さで組むと 600 バイトあった。常駐の空きが少ない)。
   ★ページ 1 に置く(Makefile の GAME_P1)。第 4 バンクの登る Pawn(climb.c)から呼ぶので、ページ 2 のものを呼ばない */
#include "game.h"
#include "ghost.h"

#define STRIP_Y   768              /* stage.c と同じ */
#define BLK_CONST 23
extern s16 g_blk_tag[];

/* ---- 列の短冊(stage.c から移した。stage.c も #define で使う) ---- */
u8 stage_strip_of(u16 wcol) {
    u16 m = (u16)(wcol - g_w0);
    /* ★差だけで見る(地図より手前の列は差が 65536 で回って maplen 以上になる)。試験では g_w0 が「今の列 - 広場までの列」で
       負に回る(g_wx は面ごとに 0..511 へ戻す)ので、wcol < w0 で外にしていた間は試験の地形が全部短冊 0 になり、5 面の試験の
       GAUGE BLOCK が空に浮いて見えた(遊んでの指摘) */
    if (m >= g_maplen) return 0;
    return g_strip_xlat[g_map[m] & 31];
}
/* ★遠景(多重スクロール): 短冊が s0..s1 の列の、s が t0..t1-1 のブロックは、短冊 strip[列 & 7](周期 64 ドットの遠景)から
   写す。面が真ん中の帯をラスターで別の速さにずらす(1・2・3 面。帯の横位置は第 4 バンクの far_step)。g_far = 0 で使わない。
   ★g_far はオーバレイの変数(RAM)を指す(本体の変数の領域に空きが無い。次の面のコードを読む間も変数の領域は残り、
   次の面の ovl_init の stage_reset_ground で外す。オーバレイのコードの中の const を指すと、読み込みで壊れる) */
u8 stage_far_at(u8 st, s16 s0) { const FarCfg *f = g_far; return (u8)(f && st >= f->s0 && st <= f->s1 && s0 >= f->t0 && s0 < f->t1); }
u8 stage_src_of(u16 wcol, s16 s0) {
    u8 st = stage_strip_of(wcol);
    return stage_far_at(st, s0) ? g_far->strip[wcol & 7] : st;
}


/* 1 本の列(世界の x = wx、幅 cw)の、リングの行 ry から h 行 */
static void rs_col(u16 wx, u8 cw, u8 ry, u8 h) {
    u16 rx = wx & 511u, dx = rx & 255u, page = rx & 256u;
    u8 sub = (u8)(wx & 7);
    while (h) {
        u8 b = (u8)(ry >> 3), n = (u8)(8 - (ry & 7));
        s16 t0 = g_blk_tag[b];
        if (n > h) n = h;
        if (b == BLK_CONST || t0 < 0 || t0 >= 188) gq_hmmv(dx, page + ry, cw, n, 0x11);
        else gq_hmmm((u16)(stage_src_of(wx >> 3, t0) * 8u + sub), (u16)(STRIP_Y + t0 + (ry & 7)), dx, page + ry, cw, n);
        ry = (u8)(ry + n); h = (u8)(h - n);
    }
}

void stage_restore(s16 x, s16 y, u8 w, u8 h) {
    u8 ry = (u8)(y + g_vscroll);
    while (w) {
        u16 wx = (u16)(g_wx + (u16)x);
        u8 cw = (u8)(8 - (wx & 7));
        if (cw > w) cw = w;
        rs_col(wx, cw, ry, h);
        x = (s16)(x + cw); w = (u8)(w - cw);
    }
}
