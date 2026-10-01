/* vdp.c — V9958 アクセスの最小層(vdp.h 参照)。 */
#include "vdp.h"

void vdp_wreg(u8 r, u8 v) {
    __asm di __endasm;
    VDP_CTRL = v;
    VDP_CTRL = 0x80 | r;
    __asm ei __endasm;
}

void vdp_set_pal(u8 idx, u8 r, u8 g, u8 b) {
    __asm di __endasm;
    VDP_CTRL = idx;
    VDP_CTRL = 0x80 | 16;
    VDP_PAL  = (u8)((r << 4) | b);
    VDP_PAL  = g;
    __asm ei __endasm;
}

void vdp_write_addr17(u8 hi, u16 lo) {
    __asm di __endasm;
    VDP_CTRL = (u8)((hi << 2) | (lo >> 14));  VDP_CTRL = 0x80 | 14;
    VDP_CTRL = (u8)(lo & 0xFF);               VDP_CTRL = (u8)(((lo >> 8) & 0x3F) | 0x40);
    __asm ei __endasm;
}

/* CE(S#2 bit0) のポーリング。S#2 選択中に VBLANK 割込みが入ると BIOS の ISR が S#0 を読めず
   永久再突入するので、1 回ごとに di で囲んで必ず S#0 へ戻す(前作で実機確定)。 */
void vdp_cmd_wait(void) {
    __asm
    00001$:
        di
        ld   a, #2
        out  (0x99), a
        ld   a, #0x8F
        out  (0x99), a
        in   a, (0x99)
        rra
        ld   a, #0
        out  (0x99), a
        ld   a, #0x8F
        out  (0x99), a
        ei
        jp   c, 00001$
    __endasm;
}

/* R#32..46 の 15 本。R#17 間接オートインクリメントで OTIR 一括発行(0x9B は FF を使わない)。 */
volatile u8 vdpcbuf[15];

static void cmd_flush(void) {
    __asm
        di
        ld   a, #32
        out  (0x99), a
        ld   a, #0x80 | 17
        out  (0x99), a
        ld   hl, #_vdpcbuf
        ld   bc, #0x0F9B
        otir
        ei
    __endasm;
}

static void cmd_rect(u16 dx, u16 dy, u16 nx, u16 ny) {
    vdpcbuf[4]  = (u8)dx; vdpcbuf[5]  = (u8)((dx >> 8) & 0x01);
    vdpcbuf[6]  = (u8)dy; vdpcbuf[7]  = (u8)((dy >> 8) & 0x03);
    vdpcbuf[8]  = (u8)nx; vdpcbuf[9]  = (u8)((nx >> 8) & 0x01);
    vdpcbuf[10] = (u8)ny; vdpcbuf[11] = (u8)((ny >> 8) & 0x03);
    vdpcbuf[13] = 0;      /* ARG: 左上→右下 */
}

void vdp_hmmm(u16 sx, u16 sy, u16 dx, u16 dy, u16 nx, u16 ny) {
    vdp_cmd_wait();
    vdpcbuf[0] = (u8)sx; vdpcbuf[1] = (u8)((sx >> 8) & 0x01);
    vdpcbuf[2] = (u8)sy; vdpcbuf[3] = (u8)((sy >> 8) & 0x03);
    cmd_rect(dx, dy, nx, ny);
    vdpcbuf[14] = 0xD0;
    cmd_flush();
}

void vdp_hmmv(u16 dx, u16 dy, u16 nx, u16 ny, u8 byte) {
    vdp_cmd_wait();
    cmd_rect(dx, dy, nx, ny);
    vdpcbuf[12] = byte;
    vdpcbuf[14] = 0xC0;
    cmd_flush();
}
