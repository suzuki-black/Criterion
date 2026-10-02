/* input.h — 入力(キーボード行列とジョイスティック 1 を BIOS を使わずに直接読む)。
   キーボード: カーソル = 移動、SPACE / Z = A(ショット)、X / M = B(確定)。
   ジョイスティック 1: 方向、トリガ 1 = A、トリガ 2 = B。 */
#ifndef INPUT_H
#define INPUT_H

#include "types.h"

#define IN_UP    0x01
#define IN_DOWN  0x02
#define IN_LEFT  0x04
#define IN_RIGHT 0x08
#define IN_A     0x10
#define IN_B     0x20

extern u8 g_in;        /* 今押されているもの */
extern u8 g_in_edge;   /* このフレームで押されたもの */

void input_read(void);  /* 1 フレームに 1 回 */

#endif /* INPUT_H */
