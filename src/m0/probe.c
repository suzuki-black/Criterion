/* probe.c — M0 の設計判断用の探り ROM(結果は docs/M0_技術検証.md)。
   (1) 走査線割込み(FH)が表示期間の外(212 行以降)でも起きるか: g_probe_line を総当たりする。
   (2) R800 の I/O 1 回の時間: VDP(0x98 読み/書き、0x99)、PCM(0xA4 読み/書き)、S1990 タイマ(0xE6)、NOP を 64 回ずつ並べ、
       openMSX のブレークポイントで各区間の所要時間を測る(_probe_* のラベル)。 */
#include "types.h"
#include "vdp.h"
#include "sys.h"
#include "frame.h"
#include "raster.h"

/* デバッガから書き換えて総当たりする(R#19 = これ - bias) */
volatile u8 g_probe_line = 230;
volatile u8 g_probe_r8 = 0x2A;    /* R#8(0x2A = スプライト非表示 / 0x28 = 表示)。VRAM 書き込みの速さの比較用 */

/* 64 回ずつ: VDP 読み(0x98)/ VDP 書き(0x99 に R#7 の 2 バイト = 32 組)/ PCM ポート読み(0xA4)/ NOP */
void probe_io(void) __naked {
    __asm
        di
    _probe_vdp_in::
        .rept 64
        in   a, (0x98)
        .endm
    _probe_vdp_out::
        .rept 32
        xor  a
        out  (0x99), a
        ld   a, #0x87
        out  (0x99), a
        .endm
    _probe_pcm_in::
        .rept 64
        in   a, (0xA4)
        .endm
    _probe_vram_out::
        .rept 64
        out  (0x98), a
        .endm
    _probe_tmr_in::
        .rept 64
        in   a, (0xE6)
        .endm
    _probe_pcm_out::
        ld   a, #128
        .rept 64
        out  (0xA4), a
        .endm
    _probe_nop::
        .rept 64
        nop
        .endm
    _probe_end::
        ei
        ret
    __endasm;
}

void main(void) {
    sys_r800();
    __asm
        ld   a, #5
        ld   (0xFCAF), a
        call 0x005F
    __endasm;
    vdp_wreg(8, 0x2A);
    vdp_wreg(9, 0x80);
    sys_ram_exec();
    ras_init();
    ras_set_isr(RAS_ISR_DIRECT);
    frame_begin();
    for (;;) {
        RasEntry *e = ras_back();
        e[0].line = g_probe_line;
        e[0].flags = RAS_PAL;
        e[0].pidx = 0; e[0].prb = 0; e[0].pg = 0;
        ras_commit(1);
        vdp_wreg(8, g_probe_r8);
        vdp_write_addr17(1, 0xF000);      /* 0x1F000: 表示に使わない場所へ書く */
        probe_io();
        frame_sync(1);
    }
}
