/* ter.c — 面の地形(g_ter。各面の ovl_init が今の床・天井の表を置く)を画面の座標で引く。DROP の爆雷と地を這う雑魚が使う。 */
#include "game.h"
#include "bank_int.h"

s16 ter_y(s16 sx, u8 up) {
    u16 c;
    u8 k;
    if (g_ter.flat) return up ? TER_NONE : (s16)(g_ter.flat - g_scam);
    if (!g_ter.lo || sx < 0 || sx > 255) return TER_NONE;
    c = (u16)(((u16)(g_wx + (u16)sx) >> 3) - g_w0);
    if (c >= g_ter.ncol) return TER_NONE;
    k = g_map[c];
    k = up ? (g_ter.hi ? g_ter.hi[k] : 0) : g_ter.lo[k];
    if (!k) return TER_NONE;                                  /* 表の 0 = その列に床(天井)が無い(5 面の空の列など) */
    return (s16)(k + (up ? 0 : g_ter.lo_add) - g_scam);
}
