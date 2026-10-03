/* s7.h — 7 面のオーバレイの中だけで使う宣言。 */
#ifndef S7_H
#define S7_H

#include "game.h"

enum { SEC7_AIR = 52, SEC7_LIBRA, SEC7_OUT };

void b7_init(void);
void b7_start(void);
void b7_update(void);
void b7_draw(void);
void b7_vram(void);
void b7_bands(void);
void b7_invert(u16 t);             /* 白い空間を黒へ反転 */

#endif /* S7_H */
