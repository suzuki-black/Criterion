/* s2.h — 2 面のオーバレイの中だけで使う宣言。 */
#ifndef S2_H
#define S2_H

#include "game.h"

enum { SEC2_AIR = 7, SEC2_SINK, SEC2_DOWN, SEC2_FLIP, SEC2_MID, SEC2_CENTER, SEC2_BOSS, SEC2_OUT };
enum { B2_CW, B2_KG };
void b2_start(u8 kind);
void b2_update(void);
void b2_draw(void);
void b2_vram(void);
u8   b2_lens(s16 *top, s16 *bot);   /* 重力レンズの帯(上の帯の始まり・下の帯の始まり)。無ければ 0 */
extern u8 b2_lens_tbl_up[50], b2_lens_tbl_dn[48];

extern u8 s2_far_zero;              /* 遠景のずれが 0(s2.c。KILOGRAM の絵はこのときだけ写す) */
#endif /* S2_H */
