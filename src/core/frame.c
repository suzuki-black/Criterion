/* frame.c — フレーム同期(絶対時刻)。frame.h 参照。 */
#include "frame.h"

#define JIFFY (*(volatile u16 *)0xFC9E)   /* BIOS の VBLANK ごとに +1 */

static u16 t0;
u16 g_frame_late;

void frame_begin(void) {
    t0 = JIFFY;
}

u16 frame_sync(u8 n) {
    u16 idle = 0;
    if ((u16)(JIFFY - t0) >= n) g_frame_late++;
    while ((u16)(JIFFY - t0) < n) idle++;
    t0 = JIFFY;
    return idle;
}
