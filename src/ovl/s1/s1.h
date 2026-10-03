/* s1.h — 1 面のオーバレイの中だけで使う宣言(s1.c / boss.c / pawn.c)。 */
#ifndef S1_H
#define S1_H

#include "game.h"

enum { BOSS_LEGION, BOSS_PLUMB };
void boss_reset(void);
void boss_start(u8 kind);
void boss_update(void);
void boss_draw(void);
void boss_leave(void);              /* 時間切れ: 倒さずに去る */
void boss_prepare(u8 kind);         /* 先に絵を作り始める(PLUMB: 都市後半に入ったとき) */
void boss_vram(void);               /* VRAM の窓で毎フレーム(絵を作る段・BG に描く) */
void boss_vram_gen(void);
u8   boss_bands(s16 *line, s8 *off);   /* LEGION の揺れの帯(数を返す) */

void pawn_sheet_load(void);
void pawn_reset(void);
void pawn_update(u8 active);
void pawn_cell(s16 x, s16 y, u8 kind);  /* 空の地の 4x4: 0 / 1 = Pawn、2 = 空 */
void pawn_move(s16 x, s16 y, u8 kind);  /* 4x8: 0 = 上 Pawn・下 空、1 = 上下 Pawn、2 = 上 空・下 Pawn */

#endif /* S1_H */
