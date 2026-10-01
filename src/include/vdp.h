/* vdp.h — V9958 アクセスの最小層。
   出典: ZeroNoHoukouR/src/core/vdp.c(実機確定の MSXgl 流イディオム)を横スクロール向けに整理。
     - 0x99 への 2 バイト書込は割込みで壊れるので、各関数が di/ei で原子化する。
     - 割込みハンドラ内からは vdp_wreg を使わない(中で ei してしまう)。ISR 用は VDP_ISR_WREG。 */
#ifndef VDP_H
#define VDP_H

#include "types.h"

__sfr __at(0x98) VDP_DAT;
__sfr __at(0x99) VDP_CTRL;
__sfr __at(0x9A) VDP_PAL;
__sfr __at(0x9B) VDP_IDAT;

/* 割込み禁止中(ISR 内)専用のレジスタ書込み。 */
#define VDP_ISR_WREG(r, v) do { VDP_CTRL = (v); VDP_CTRL = 0x80 | (r); } while (0)

void vdp_wreg(u8 r, u8 v);
void vdp_set_pal(u8 idx, u8 r, u8 g, u8 b);

/* 17bit VRAM 書込みアドレス設定((hi<<16)|lo)。以降 VDP_DAT へ連続書込み。
   ★アドレスカウンタの桁上げは 16KB 境界で R#14 へ伝播しないので、跨ぐときは貼り直すこと。 */
void vdp_write_addr17(u8 hi, u16 lo);

void vdp_cmd_wait(void);

/* HMMM: VRAM→VRAM バイト単位高速コピー。SCREEN5 では X/幅は偶数で扱う。Y は 0..1023(4 ページ)。 */
void vdp_hmmm(u16 sx, u16 sy, u16 dx, u16 dy, u16 nx, u16 ny);
/* HMMV: バイト単位の矩形塗り。byte は 2 画素分(例: 0x00 = 色0 2 画素)。単色クリア専用。 */
void vdp_hmmv(u16 dx, u16 dy, u16 nx, u16 ny, u8 byte);

#endif /* VDP_H */
