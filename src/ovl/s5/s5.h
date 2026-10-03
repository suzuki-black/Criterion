/* s5.h — 5 面のオーバレイの中だけで使う宣言。 */
#ifndef S5_H
#define S5_H

#include "game.h"

enum { SEC5_AIR = 33, SEC5_SINK, SEC5_APPROACH, SEC5_BOW, SEC5_DECK, SEC5_HANGAR, SEC5_UNDER, SEC5_ENGINE,
       SEC5_BOSS, SEC5_WRECK, SEC5_OUT };

#define S_DECKVIEW (-60)           /* 甲板の上を飛ぶときのカメラ(画面の上端の s) */

/* s5.c */
s16  s5_sx(u16 col);               /* 地図の列 → 画面の x(左端) */
s16  s5_sy(s16 s);                 /* 世界の s → 画面の y */
u16  s5_col_at(s16 sx);            /* 画面の x → 地図の列 */
extern s8 s5_press_c, s5_press_f;  /* 機関部のプレス(天井・床)の張り出し(ドット) */

/* s5boss.c: 砲台・中ボス・ボス */
void b5_init(void);
void b5_update(void);              /* 区間に関係なく(砲台は画面に入ったものだけ) */
void b5_draw(void);
void b5_vram(void);
void b5_hangar_start(void);
void b5_gauge_start(void);
void b5_gauge_load(void);
void b5_skip_sheets(void);         /* 主砲塔と遠くの戦艦の絵を読まない(7 面の試験) */          /* GAUGE BLOCK のブロックをページ 2 へ読み始める */
u8   b5_gauge_bands(s16 sc, u16 nx);   /* GAUGE BLOCK の帯(出したら 1) */

#endif /* S5_H */
