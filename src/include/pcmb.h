/* pcmb.h — PCM 方式 B(指示書 8.6): S1990 のフリーランタイマ(E6h/E7h, 約 255.7kHz)を
   メイン側の待ちループ(frame_sync の待ち・VDP コマンドの完了待ち)で見て、時刻が来ていれば出力する。
   割込みを増やさないが、ポーリングが止まる区間(長い割込み処理・重い計算)ではサンプルが飛ぶ。
   飛んだぶんは時刻に合わせて読み飛ばす(音の長さは保ち、波形が欠ける)。 */
#ifndef PCMB_H
#define PCMB_H

#include "types.h"

extern volatile u16 g_pcmb_skips;   /* 読み飛ばしたサンプル数(計測用) */

void pcmb_play(const u8 *p, u16 n);
void pcmb_stop(void);
void pcmb_queue(const u8 *p, u16 n);   /* 鳴り終わったら切れ目なく続けて鳴らす */
u8   pcmb_queued(void);
u16  pcmb_remaining(void);
void pcmb_poll(void);               /* 待ちループから呼ぶ */

#endif /* PCMB_H */
