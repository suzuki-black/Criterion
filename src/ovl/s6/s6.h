/* s6.h — 6 面のオーバレイの中だけで使う宣言。 */
#ifndef S6_H
#define S6_H

#include "game.h"

enum { SEC6_AIR = 44, SEC6_SINK, SEC6_WALL, SEC6_SHADOW, SEC6_MID, SEC6_DIAL, SEC6_BOSS, SEC6_OUT };

#define S6_CAM    (-120)           /* カメラ: 画面の 48..187 ラインに s = -72..67 */
#define SPLIT_H   48               /* 上の SCREEN12 の帯 */
#define NUM_TOP   30               /* 数字が残っている台座で自機が下りられる限りの行(s) */

/* s6.c */
s16  s6_sx(u16 col);
s16  s6_sy(s16 s);
u16  s6_col_at(s16 sx);

/* s6boss.c */
void b6_init(void);
void b6_update(void);
void b6_draw(void);
void b6_vram(void);
void b6_bands(void);               /* ボスの帯(今は無し) */
void b6_mid_start(void);
void b6_boss_start(void);
u8   b6_num_alive(u16 col);        /* この列の台座に数字が残っている */

#endif /* S6_H */
