/* lift.c — 5 面の格納庫の台(エレベーター)の絵を作る(第 4 バンク。2026-11-09 に s5boss.c から移した。甲板の機銃の処理を
   軽くしたらオーバレイがあふれたため。台が出る前の 4 フレームだけ呼ぶ) */
#include "game.h"
#include "vdp.h"

/* 台の絵のドット */
static u8 lift_px(u8 x, u8 y) {
    if (y == 0) return 14;
    if (y == LIFT_H - 1) return 2;
    if (y <= 2 || y >= LIFT_H - 3) return (u8)((((x + y) >> 2) & 1) ? 12 : 0);   /* 危険の縞(黄と黒) */
    if (x < 2 || x >= LIFT_W - 2) return 14;
    if (x >= 18 && x < 30 && y >= 4 && y <= 7) return (u8)(y == 4 && x < 22 ? 15 : 13);   /* ランプ */
    if (y == 5 && (x == 6 || x == LIFT_W - 7)) return 14;     /* 鋲 */
    return 9;
}
void lift_sheet(u8 y0) {           /* 48x12 をページ 2 へ、y0 から 3 行(★1 フレームで 12 行書くと 2 フレーム遅れた) */
    u8 x, y;
    for (y = y0; y < y0 + 3; y++) {
        vdp_write_addr17(1, (u16)((LIFT_IMG + y) * 128u));
        for (x = 0; x < LIFT_W; x += 2) VDP_DAT = (u8)((lift_px(x, y) << 4) | lift_px((u8)(x + 1), y));
    }
}
