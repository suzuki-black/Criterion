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

/* 横スクロール位置 x(ドット)→ R#26 / R#27(SP2 リング 512 幅。M0-1 で 1 ドット単位に検証済み) */
#define VDP_R26(x) ((u8)((((x) + 7u) >> 3) & 63u))
#define VDP_R27(x) ((u8)((0u - (x)) & 7u))

void vdp_wreg(u8 r, u8 v);
void vdp_set_pal(u8 idx, u8 r, u8 g, u8 b);
void vdp_pal_raw(u8 idx, u8 rb, u8 g);   /* 控え(g_pal_sh)を変えずに書く(点滅の白) */
extern u8 g_pal_sh[32];

/* 17bit VRAM 書込みアドレス設定((hi<<16)|lo)。以降 VDP_DAT へ連続書込み。
   ★アドレスカウンタの桁上げは 16KB 境界で R#14 へ伝播しないので、跨ぐときは貼り直すこと。 */
void vdp_write_addr17(u8 hi, u16 lo);

/* n バイト(0 = 256)を VRAM へ続けて書く(vdp_write_addr17 の後)。 */
void vdp_out_block(u8 n, const u8 *p);

void vdp_cmd_wait(void);
u8   vdp_cmd_busy(void);                 /* 1 = コマンド実行中(待たない) */
void vdp_cmd_issue(const u8 *p);        /* 詰め済みの R#32..46(15 バイト)を出す(前の終わりは待つ) */
/* vdp_cmd_wait の待ちの間に毎回呼ぶ関数(0 = なし)。PCM 方式 B のポーリングに使う。 */
extern void (*volatile g_vdp_wait_hook)(void);
/* R#15(読むステータスレジスタ)の待機値。BIOS を使う間は 0、直接割込みでは 1(raster.c が設定)。
   S#2 などを読んだら必ずこの値へ戻す。 */
extern volatile u8 g_vdp_status_rest;

/* HMMM: VRAM→VRAM バイト単位高速コピー。SCREEN5 では X/幅は偶数で扱う。Y は 0..1023(4 ページ)。 */
void vdp_hmmm(u16 sx, u16 sy, u16 dx, u16 dy, u16 nx, u16 ny);
/* HMMV: バイト単位の矩形塗り。byte は 2 画素分(例: 0x00 = 色0 2 画素)。単色クリア専用。 */
void vdp_hmmv(u16 dx, u16 dy, u16 nx, u16 ny, u8 byte);

#endif /* VDP_H */
