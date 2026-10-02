/* spr.h — スプライト(モード 2、16x16)の管理。指示書 8.7。
   ★画面を 2 帯(0..SPR_SPLIT-1 / SPR_SPLIT..)に分け、帯ごとに属性表を切り替えて画面内 64 枚まで出す。
     境界をまたぐスプライトは両方の表に入れる。表は帯ごとに二重化し、表示していない側へ書く
     (VDP への書き込みは 1 バイト 8.6us。表示中の表を書き換えると走査線抜けや色ずれになる。前作の教訓)。
   ★枠(0..31)は用途ごとのグループに固定し、色表はグループごとに起動時に 1 回だけ書く。毎フレーム順番を回すと
     枠ごとの色が変わり、色表(1 枠 16 バイト ≒ 2.2 ライン)を毎フレームほぼ全部書き直して 1 フレームの半分を
     使っていた(M1)。ちらつきの分散はグループの中で順番を回して行う。色を個別に持てるのは G_MISC だけ。
   ★1 ラインに 8 枚の制限は番号の小さい枠が勝つ。並びは 自機 → 自機弾 → 敵 → 敵弾 → その他。
   使い方(1 フレーム): spr_begin() → spr_put() → spr_end() → spr_regs で帯の R#5/R#11。 */
#ifndef SPR_H
#define SPR_H

#include "types.h"

#define SPR_SPLIT   96        /* 帯の境界の行(ラスターの項目もこの行に置く) */
#define SPR_LIMIT_Y 188       /* これより下(HUD 帯)には出さない。HUD 用は spr_put_hud */

/* グループ(枠の範囲は spr.c) */
enum { G_SHIP_A, G_SHIP_B, G_SHOT, G_ENEMY, G_EBUL, G_MISC, G_COUNT };

/* G_MISC の色の組 */
enum { CS_QUANTA, CS_EXPL, CS_CURSOR, CS_SHARD, CS_DIM, CS_COUNT };

typedef struct { u8 r5a, r11a, r5b, r11b; } SprRegs;
extern SprRegs spr_regs;      /* spr_end() が書いた表の R#5/R#11(上の帯 / 下の帯) */
extern u8 g_spr_dropped;      /* グループの枠に入りきらなかった枚数(計測用) */

void spr_init(const u8 (*pat)[32], u8 npat);   /* パターン表・色表の初期化、R#1/R#5/R#6/R#11 */
void spr_begin(void);
void spr_put(s16 x, s16 y, u8 pat, u8 group, u8 cs);       /* 左上(ドット)。cs は G_MISC のときだけ使う */
void spr_put_hud(s16 x, s16 y, u8 pat, u8 cs);             /* HUD 帯(G_MISC) */
void spr_end(void);

#endif /* SPR_H */
