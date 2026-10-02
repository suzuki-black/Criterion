/* world.h — M0 の検証 ROM が共有する「世界」(world.c 参照)。 */
#ifndef WORLD_H
#define WORLD_H

#include "types.h"

/* 横スクロール位置 → レジスタ(M0-1 で 1 ドット単位に検証済み) */
#define R26(x) ((u8)((((x) + 7u) >> 3) & 63u))
#define R27(x) ((u8)((0u - (x)) & 7u))

typedef struct { s16 target; s16 accel; u8 hold; } Seg;
typedef struct {
    u16 posi;    /* 位置(ドット) */
    u8  posf;    /* 位置の小数部(1/256) */
    s16 speed;   /* 速度(1/256 ドット/フレーム) */
    u8  seg, hold;
} WorldRun;

extern u16 g_world_cols;     /* 描き込んだ列の総数 */

u8   world_strip_of(u16 c);
void world_set_pages(u8 ring, u8 strip);  /* リング(SP2 の偶数ページ)とストリップのページ。既定 0 と 2 */
void world_set_top(u8 y0);                /* 列の描き込みを y0 行目からにする(既定 0) */
void world_gen_strips(void);              /* ストリップのページに 32 本の列ストリップを作る */
void world_palette(void);
void world_reset(u16 x);                  /* 描き込み済み範囲を空にする */
void world_stream(u16 x, s16 ahead, s16 behind);
void world_speed_step(WorldRun *r);       /* 速度プログラム(0.5→8 加速・急停止・逆走)を 1 フレーム進める */

#endif /* WORLD_H */
