/* pcmc.c — PCM 方式 A/C(刻み方式)の制御(raster.h)。割込みの側は raster.c。
   ★本体は方式 B(snd.c の pcmx)に移したので、これは M0 の検証 ROM にだけリンクする(常駐の空きのため。M7)。 */
#include "raster.h"
#include "vdp.h"
#include "frame.h"

extern volatile u8 pcm_on, pcm_fill, pcm_pend, pcm_end, ras_kick;
extern const u8 *volatile pcm_ptr;
extern volatile u16 pcm_left;
extern const u8 *volatile pcm_next_ptr;
extern volatile u16 pcm_next_left;
static void ras_kick_ticks(void);

/* ---- PCM(方式 A/C: 走査線割込みに相乗り) ---- */
void pcm_play(const u8 *p, u16 n, u8 fill) {
    g_stall_hook = ras_kick_ticks;
    __asm di __endasm;
    pcm_end = 0;
    pcm_ptr = p;
    pcm_left = n;
    pcm_fill = fill;
    if (!pcm_on) pcm_pend = 1;
    __asm ei __endasm;
}

/* 刻み方式で止めた VBLANK 割込みを戻す(R#1 = RG1SAV)。 */
static void vblank_irq_on(void) {
    VDP_CTRL = *(volatile u8 *)0xF3E0;
    VDP_CTRL = 0x81;
}

/* 刻みの鎖が途切れたとき(frame_sync の見張りが呼ぶ): VBLANK 割込みを戻し、次の VBLANK で張り直す。 */
static void ras_kick_ticks(void) {
    __asm di __endasm;
    if (pcm_on) {
        ras_kick = 1;
        g_ras_rearm++;
        vblank_irq_on();
    }
    __asm ei __endasm;
}

void pcm_stop(void) {
    __asm di __endasm;
    if (pcm_on) pcm_end = 1;       /* 刻み 212 で項目方式へ戻る(VBLANK 割込みもそこで戻す) */
    pcm_pend = 0;
    pcm_left = 0;
    pcm_next_left = 0;
    __asm ei __endasm;
}

void pcm_queue(const u8 *p, u16 n) {
    __asm di __endasm;
    pcm_next_ptr = p;
    pcm_next_left = n;
    __asm ei __endasm;
}

u8 pcm_queued(void) {
    u8 q;
    __asm di __endasm;
    q = pcm_next_left != 0;
    __asm ei __endasm;
    return q;
}

u16 pcm_remaining(void) {
    u16 n;
    __asm di __endasm;
    n = pcm_left;
    __asm ei __endasm;
    return n;
}
