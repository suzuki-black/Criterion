/* s4.h — 4 面のオーバレイの中だけで使う宣言。 */
#ifndef S4_H
#define S4_H

#include "game.h"

enum { SEC4_AIR = 24, SEC4_SINK, SEC4_UPPER, SEC4_SEA, SEC4_HAZE, SEC4_MID, SEC4_RIVER, SEC4_BOSS, SEC4_OUT };
enum { B4_VERNIER, B4_RULER, B4_STRATUM };

/* s4.c: 雲の帯 */
u16  s4_band_x(u8 layer);           /* 層の表示の左端のリングの x */
void s4_band_hold(u8 layer, u16 x); /* 層の横位置をボスが決める(0xFFFF = 流れに戻す) */
s16  s4_ring_sx(u16 rx, u8 layer);  /* リングの x → その層での画面の x(-256..255) */
void s4_ring_restore(u16 rx, s16 s, u16 w, u8 h);           /* リングの矩形を雲の絵(ページ 3)に戻す */

/* s4boss.c */
void b4_start(u8 kind);
void b4_update(void);
void b4_draw(void);
void b4_vram(void);
void b4_load_face(void);            /* STRATUM の顔をページ 2 へ読み始める */
u8   b4_bands(s16 sc);              /* ボスが帯を出したら 1(雲の帯の代わり) */

#endif /* S4_H */
