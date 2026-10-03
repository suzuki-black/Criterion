/* rotor.h — 回転拡縮エンジン ROTOR(指示書 8.9)。
   ・rotor_4bpp: 64x64 の 4bpp(1 行 32 バイト)を、角度と倍率をつけて n x n の 4bpp へ写す(BG 用)。
     元の外は bg の色で埋める。元の絵の中心(32, 32)が写し先の中心に来る。
   ・角度は 0..63 で一周(g_sin64 と同じ刻み)。step は写し先 1 ドットあたりに元を何ドット進むか(8.8 固定小数点、
     256 = 等倍、大きいほど小さく写る)。
   ★元は ROTOR_SRC(0xD800、2KB、256 境界)に置くこと(asm がアドレスの上位だけを足す)。
     写し先は任意(RAM)。標準の置き場 ROTOR_DST(0xE000、2KB)。
   ★1 ドットおよそ 11us(R800、openMSX の実測。スプライト用の 1bpp で 11.5us)。64x64 で約 45ms。
     毎フレームではなく、場面の入りで前もって作るか、何フレームかに分けて作る。 */
#ifndef ROTOR_H
#define ROTOR_H

#include "types.h"

#define ROTOR_SRC ((u8 *)0xD800)
#define ROTOR_DST ((u8 *)0xE000)

void rotor_4bpp(u8 *dst, u8 n, u8 angle, u16 step, u8 bg);
/* 分けて作る: rotor_begin の後、rotor_rows を何回かに分けて呼ぶ(dst は呼ぶたびにその続きを渡す) */
void rotor_begin(u8 n, u8 angle, u16 step, u8 bg);
void rotor_rows(u8 *dst, u8 rows);

/* スプライト用(32x32 の 1bpp)。★元は ROT1_SRC(ページ 0 の RAM の 0x3800、1KB。CODE0 はその手前まで) */
#define ROT1_SRC ((u8 *)0x3800)
#define ROT1_DST ((u8 *)0x3C00)        /* 写し先の標準(128 バイト) */
void rotor_1bpp_src(const u8 *bits);  /* 32x32 の 1bpp(128 バイト)を元として広げる(ROT1_SRC へ) */
#define ROT1_SRC2 ((u8 *)0xE800)       /* 2 つ目の元の置き場(ページ 3、1KB。3 面の回転計の針) */
#define ROT1_DST2 ((u8 *)0xEC00)       /* その写し先(128 バイト)。★スタック(0xF080 から下)との間は 0x300 ほど */
void rotor_1bpp_src_at(const u8 *bits, u8 *dst);
extern u8 rot1_hi;                     /* rotor_1bpp32 が読む元の上位: 0x38(ROT1_SRC)/ 0xE8(ROT1_SRC2) */
void rotor_1bpp32(u8 *dst, u8 angle, u16 step, u8 row0, u8 rows);   /* 何行かずつ作れる */

#endif /* ROTOR_H */
