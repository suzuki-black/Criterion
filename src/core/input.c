/* input.c — 入力(input.h 参照)。
   ★BIOS の GTSTCK/GTTRIG は使わない(直接割込み方式では BIOS を呼べない。前作でも禁止)。 */
#include "input.h"

__sfr __at(0xA9) PPI_B;
__sfr __at(0xAA) PPI_C;
__sfr __at(0xA0) PSG_ADDR;
__sfr __at(0xA1) PSG_WR;
__sfr __at(0xA2) PSG_RD;

u8 g_in;
u8 g_in_edge;

/* キーボード行列の 1 行(押されているビットが 1) */
static u8 key_row(u8 row) {
    u8 v;
    __asm di __endasm;
    PPI_C = (u8)((PPI_C & 0xF0) | row);
    v = PPI_B;
    __asm ei __endasm;
    return (u8)~v;
}

/* ジョイスティック 1(PSG R#14、R#15 の bit6 = 0 で端子 1)。押されているビットが 1。 */
static u8 joy1(void) {
    u8 v;
    __asm di __endasm;
    PSG_ADDR = 15;
    v = PSG_RD;
    PSG_WR = (u8)(v & 0xBF);
    PSG_ADDR = 14;
    v = PSG_RD;
    __asm ei __endasm;
    return (u8)(~v & 0x3F);
}

void input_read(void) {
    u8 r8 = key_row(8), r5 = key_row(5), r4 = key_row(4), j = joy1();
    u8 v = (u8)(j & 0x3F);                         /* 上 下 左 右 A B の並びはジョイスティックと同じ */
    if (r8 & 0x20) v |= IN_UP;
    if (r8 & 0x40) v |= IN_DOWN;
    if (r8 & 0x10) v |= IN_LEFT;
    if (r8 & 0x80) v |= IN_RIGHT;
    if ((r8 & 0x01) || (r5 & 0x80)) v |= IN_A;    /* SPACE / Z */
    if ((r5 & 0x20) || (r4 & 0x04)) v |= IN_B;    /* X / M */
    g_in_edge = (u8)(v & ~g_in);
    g_in = v;
}
