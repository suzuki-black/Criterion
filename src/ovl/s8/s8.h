/* s8.h — 8 面のオーバレイ(前半 s8 / 後半 s8b)で使う宣言。 */
#ifndef S8_H
#define S8_H

#include "game.h"

enum { SEC8_AIR = 55, SEC8_SHAFT, SEC8_WARP, SEC8_MID, SEC8_CORE, SEC8_BOSS1, SEC8_BOSS2, SEC8_BOSS3, SEC8_ESCAPE, SEC8_OUT };

#define S8_SHAFT_TOP (-560)        /* 縦穴の上(カメラ) */

#endif /* S8_H */
