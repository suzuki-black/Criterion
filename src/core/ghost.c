/* ghost.c — VDP コマンドの待ち行列(ghost.h 参照)。 */
#include "ghost.h"
#include "vdp.h"
#include "sys.h"

static u8 q[GQ_N][15];
static u8 head, tail;
u8 gq_count;                      /* 積んである数(計測用に非 static) */
#define count gq_count

void gq_clear(void) { head = tail = count = 0; }
u8 gq_free(void) { return (u8)(GQ_N - count); }

static u8 *push(void) {
    u8 *p;
    if (count >= GQ_N) return 0;
    p = q[tail];
    if (++tail >= GQ_N) tail = 0;
    count++;
    return p;
}

static void rect(u8 *p, u16 dx, u16 dy, u16 nx, u16 ny) {
    p[4] = (u8)dx;  p[5] = (u8)((dx >> 8) & 0x01);
    p[6] = (u8)dy;  p[7] = (u8)((dy >> 8) & 0x03);
    p[8] = (u8)nx;  p[9] = (u8)((nx >> 8) & 0x01);
    p[10] = (u8)ny; p[11] = (u8)((ny >> 8) & 0x03);
    p[13] = 0;                                   /* ARG: 左上 → 右下 */
}

void gq_hmmm(u16 sx, u16 sy, u16 dx, u16 dy, u16 nx, u16 ny) {
    u8 *p = push();
    if (!p) return;
    p[0] = (u8)sx; p[1] = (u8)((sx >> 8) & 0x01);
    p[2] = (u8)sy; p[3] = (u8)((sy >> 8) & 0x03);
    rect(p, dx, dy, nx, ny);
    p[12] = 0; p[14] = 0xD0;
}

void gq_hmmv(u16 dx, u16 dy, u16 nx, u16 ny, u8 byte) {
    u8 *p = push();
    if (!p) return;
    rect(p, dx, dy, nx, ny);
    p[12] = byte; p[14] = 0xC0;
}

/* ★前のコマンドの終わりを待つのも予算のうち(vdp_cmd_issue に任せると、2KB の HMMM の残りを丸ごと待った) */
void gq_pump(u16 budget) {
    u16 t0 = sys_timer();
    while (count) {
        if (vdp_cmd_busy()) {
            if ((u16)(sys_timer() - t0) >= budget) return;
            continue;
        }
        vdp_cmd_issue(q[head]);
        if (++head >= GQ_N) head = 0;
        count--;
    }
}
