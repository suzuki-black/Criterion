/* ghost.h — ソフトウェアスプライト GHOST のための VDP コマンドの待ち行列(指示書 8.8)。
   描く側は HMMM / HMMV を積むだけ。gq_pump が CE を見ながら時間の予算の間だけ順に出す(最後の 1 つは待たない)。
   ★VDP のコマンド(HMMM は 1 バイト約 5us)がフレームの最大の制約。積む量は描く側が予算で管理する。
   ★座標は VRAM の通し座標(y は 0..1023 でページ = y / 256)。x は HMMM / HMMV ではバイト単位に丸められる(偶数)。 */
#ifndef GHOST_H
#define GHOST_H

#include "types.h"

#define GQ_N 48                    /* 積める数 */

void gq_hmmm(u16 sx, u16 sy, u16 dx, u16 dy, u16 nx, u16 ny);
void gq_hmmv(u16 dx, u16 dy, u16 nx, u16 ny, u8 byte);
void gq_line(u16 x, u16 y, s16 dx, s16 dy, u8 col);   /* (x, y) から (dx, dy) の線(論理なし) */
u8   gq_free(void);                /* 空きの数 */
void gq_pump(u16 budget);          /* budget = S1990 タイマのカウント(1 ライン ≒ 16.27) */
void gq_clear(void);
/* 積んだ物が VDP へ出たか: 積んだ直後に gq_done + gq_count を覚え、(s8)(gq_done - 覚えた値) >= 0 になったら出た */
extern u8 gq_count, gq_done;

#endif /* GHOST_H */
