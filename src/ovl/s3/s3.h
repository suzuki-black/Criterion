/* s3.h — 3 面のオーバレイの中だけで使う宣言。 */
#ifndef S3_H
#define S3_H

#include "game.h"

enum { SEC3_AIR = 15, SEC3_SINK, SEC3_ACCEL, SEC3_STOP, SEC3_REV, SEC3_MID, SEC3_TOP, SEC3_BOSS, SEC3_OUT };
enum { B3_MET, B3_TACHO };
void b3_start(u8 kind);
void b3_update(void);
void b3_draw(void);
void b3_vram(void);
void b3_load_dial(void);            /* 回転計の盤をページ 2 へ読み始める */

extern u8 s3_far_zero;              /* 遠景のずれが 0(s3.c。ボスの盤はこのときだけ写す) */
#endif /* S3_H */
