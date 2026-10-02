/* frame.c — フレーム同期(絶対時刻)。frame.h 参照。 */
#include "frame.h"
#include "sys.h"

#define JIFFY (*(volatile u16 *)0xFC9E)   /* BIOS の VBLANK ごとに +1 */

static u16 t0;
u16 g_frame_late;
void (*volatile g_poll_hook)(void);
void (*volatile g_stall_hook)(void);

void frame_begin(void) {
    t0 = JIFFY;
}

u16 frame_sync(u8 n) {
    u16 idle = 0;
    u16 w0 = sys_timer();
    if ((u16)(JIFFY - t0) >= n) g_frame_late++;
    while ((u16)(JIFFY - t0) < n) {
        idle++;
        if (g_poll_hook) g_poll_hook();
        /* 見張り: JIFFY が約 35ms(S1990 タイマ 9000 カウント)進まなければ g_stall_hook を呼ぶ
           (PCM の刻み方式では JIFFY を刻みの中で進めるので、刻みが途切れると止まるため) */
        if ((idle & 63) == 0 && (u16)(sys_timer() - w0) > 9000u) {
            if (g_stall_hook) g_stall_hook();
            w0 = sys_timer();
        }
    }
    t0 = JIFFY;
    return idle;
}
