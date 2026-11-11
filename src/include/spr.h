/* spr.h — スプライト(モード 2、16x16)の管理。指示書 8.7。
   ★画面を 2 帯(0..SPR_SPLIT-1 / SPR_SPLIT..)に分け、帯ごとに属性表を切り替えて画面内 64 枚まで出す。
     境界をまたぐスプライトは両方の表に入れる。表は帯ごとに二重化し、表示していない側へ書く
     (VDP への書き込みは 1 バイト 8.6us。表示中の表を書き換えると走査線抜けや色ずれになる。前作の教訓)。
   ★枠(0..31)は用途ごとのグループに固定し、色表はグループごとに起動時に 1 回だけ書く。毎フレーム順番を回すと
     枠ごとの色が変わり、色表(1 枠 16 バイト ≒ 2.2 ライン)を毎フレームほぼ全部書き直して 1 フレームの半分を
     使っていた(M1)。ちらつきの分散はグループの中で順番を回して行う。色を個別に持てるのは G_MISC だけ。
   ★1 ラインに 8 枚の制限は番号の小さい枠が勝つ。並びは 自機 → その他 → 自機弾 → 敵 → 敵弾(spr.c の slot_order)。
   使い方(1 フレーム): spr_begin() → spr_put() → spr_end() → spr_regs で帯の R#5/R#11。 */
#ifndef SPR_H
#define SPR_H

#include "types.h"

#define SPR_SPLIT   96        /* 帯の境界の行(ラスターの項目もこの行に置く) */
#define SPR_LIMIT_Y 188       /* これより下(HUD 帯)には出さない。HUD 用は spr_put_hud */
#define HUD_SCREEN_Y 188      /* HUD 帯の画面の行(HUD 帯は属性表が別) */

/* グループ(枠の範囲は spr.c)。★単色のものは置かない(コナミの MSX2 の作法。M7 の後): どのグループも行ごとに色を変え、
   G_PAIR は 2 枚の OR 合成(CC)で 3 色にする */
enum { G_SHIP_A, G_SHIP_B, G_SHOT, G_PAIR, G_ENEMY, G_EBUL, G_OPT, G_MISC, G_COUNT };   /* G_OPT: 強化(割り振り 8..15 の間だけ 4 枠) */

/* G_MISC の色の組 */
enum { CS_QUANTA, CS_EXPL, CS_CURSOR, CS_SHARD, CS_DIM, CS_COUNT };

typedef struct { u8 r5a, r11a, r5b, r11b, r5h; } SprRegs;   /* R#11 はどれも 3 */
extern SprRegs spr_regs;      /* spr_end() が書いた表の R#5/R#11(上の帯 / 下の帯 / HUD 帯) */
extern u8 g_spr_dropped;      /* グループの枠に入りきらなかった枚数(計測用) */
extern u8 spr_voff;           /* プレイエリアの縦スクロール(R#23)。spr_put の Y に足す */

void spr_sat_reset(void);                      /* 属性表に書いた Y の記録(ページ 0 の 0x3C80)を消す。ページ 0 を RAM にした後に 1 回 */
void spr_init(void);                           /* 属性表の初期化、R#1/R#5/R#6/R#11。★パターン表(0x1F800、63 番 = 空白)と
                                                  色表は呼ぶ側が ROM から写す(常駐の空きのため。main.c) */
extern const u16 sat_addr[2][2];               /* 帯ごと・二重化した属性表の番地(0x1xxxx の下 16 ビット)。色表はその 0x200 前 */
u8   spr_written(void);                          /* spr_end が最後に書いた表(0/1) */
u8   spr_pairs(u8 l);                            /* 割り振り l の組の枠の数(組は枠 8 から) */
void spr_layout(u8 l);                         /* 枠の割り振り(0 = ふつう、1..3 = 部品の多いボス。spr.c)。色表は呼ぶ側(main.c) */
extern u8 g_spr_layout;
void spr_begin(void);
void spr_put(s16 x, s16 y, u8 pat, u8 group, u8 cs);       /* 左上(ドット)。cs は G_MISC のときだけ使う。
                                                              G_PAIR は pat(下の層)と pat + 1(上の層、CC)を隣の枠に置く */
void spr_sel(u8 pat, u8 group, u8 cs);                    /* spr_putf で置く絵・グループ・色の組 */
void spr_putf(s16 xf, s16 yf);                             /* 中心(1/16 ドット)で置く = spr_put(PX(x) - 8, PX(y) - 8, ...) */
void spr_put_xy(void);
void spr_put_hud(s16 x, s16 y, u8 pat, u8 cs);             /* HUD 帯(1 枚、色は G_MISC の組) */
void spr_end(void);

#endif /* SPR_H */
