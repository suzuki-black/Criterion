/* bank_int.h — 第 2 バンクの中だけで使う口(本体からは呼ばない)。 */
#ifndef BANK_INT_H
#define BANK_INT_H

#include "types.h"

#define TER_NONE 0x7FFF
s16 ter_y(s16 sx, u8 up);          /* ter.c: 画面の x の床(up = 0)・天井(up = 1)の画面の y。地形が無ければ TER_NONE */

#endif /* BANK_INT_H */
