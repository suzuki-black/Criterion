/* sys.c — 起動時のシステム設定。出典: ZeroNoHoukouR/src/core/sys.c */
#include "sys.h"

void sys_r800(void) {
    __asm
        ld   a, (0x002D)     ; MSX_VER
        cp   #3
        jr   c, 00001$       ; turboR 未満では CHGCPU(0180h)が無い
        ld   a, #0x82        ; R800 DRAM モード + 変更ビット
        call 0x0180          ; CHGCPU
    00001$:
    __endasm;
}

void sys_hook_vblank(void (*fn)(void)) {
    volatile u8 *h = (volatile u8 *)0xFD9F;   /* H.TIMI */
    __asm di __endasm;
    h[1] = (u8)(u16)fn;
    h[2] = (u8)((u16)fn >> 8);
    h[0] = 0xC3;                               /* JP fn */
    __asm ei __endasm;
}
