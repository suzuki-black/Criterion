/* frame.h — フレーム同期(絶対時刻)。
   出典: ZeroNoHoukouR 111591e「フレームの待ちを絶対時刻の同期にして 20fps → 30fps」。
   ★「処理のあとで VBLANK を n 回待つ」形にしてはいけない。処理が 1 VBLANK を超えたぶんが
     尺に足し算され、30fps のつもりが 20fps に落ちた(前作で実測)。
   ★フレーム先頭(基準)から n ティック目まで待ち、既に過ぎていれば待たずに出る。
     遅れは次フレームへ持ち越さない(出口で基準を今のティックに置き直す)。 */
#ifndef FRAME_H
#define FRAME_H

#include "types.h"

/* 基準を今のティック(JIFFY)に置く。ループに入る直前に 1 回呼ぶ。 */
void frame_begin(void);

/* 基準から n ティック目まで待ち、基準を出口のティックへ置き直す。
   戻り値 = 待ちループの回数(CPU 残量の目安。遅れて入ったフレームは 0)。 */
u16 frame_sync(u8 n);

/* 待ちループの中で毎回呼ぶ関数(0 = なし)。PCM 方式 B のポーリングに使う。 */
extern void (*volatile g_poll_hook)(void);

/* 待ちが約 35ms を超えたら呼ぶ関数(0 = なし)。PCM の刻みの見張り(raster.c)。 */
extern void (*volatile g_stall_hook)(void);

/* 基準から n ティック以上かかって待てなかったフレームの累計。 */
extern u16 g_frame_late;

#endif /* FRAME_H */
