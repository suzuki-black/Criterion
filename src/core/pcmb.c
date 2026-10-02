/* pcmb.c — PCM 方式 B(pcmb.h 参照)。 */
#include "pcmb.h"
#include "frame.h"
#include "vdp.h"
#include "sys.h"

__sfr __at(0xA4) PCM_DA;

/* 1 サンプルの長さ = 255682 / 7867.13 ≒ 32.50 カウント(1/256 単位で 8320) */
#define PERIOD_I 32u
#define PERIOD_F 128u

static const u8 *ptr;
static volatile u16 left;
static const u8 *next_ptr;
static volatile u16 next_left;
static u16 due_i;
static u8  due_f;
volatile u16 g_pcmb_skips;

static void advance(void) {
    u16 f = (u16)due_f + PERIOD_F;
    due_i += PERIOD_I + (f >> 8);
    due_f = (u8)f;
}

void pcmb_poll(void) {
    s16 d;
    if (!left) {
        if (!next_left) return;
        ptr = next_ptr; left = next_left; next_left = 0;   /* 予約を切れ目なく続ける(時刻はつなぐ) */
    }
    d = (s16)(sys_timer() - due_i);
    if (d < 0) return;
    while (d >= 33 && left > 1) {          /* 1 周期以上遅れた: 時刻に合わせて読み飛ばす */
        ptr++; left--; g_pcmb_skips++;
        advance();
        d = (s16)(d - 32);
    }
    PCM_DA = *ptr++;
    left--;
    advance();
}

void pcmb_play(const u8 *p, u16 n) {
    if (!left) { due_i = sys_timer(); due_f = 0; }   /* 再生中の差し替えなら時刻はつなぐ */
    ptr = p;
    left = n;
    g_poll_hook = pcmb_poll;
    g_vdp_wait_hook = pcmb_poll;
}

void pcmb_stop(void) {
    left = 0;
    next_left = 0;
    g_poll_hook = 0;
    g_vdp_wait_hook = 0;
}

u16 pcmb_remaining(void) { return left; }

void pcmb_queue(const u8 *p, u16 n) { next_ptr = p; next_left = n; }
u8   pcmb_queued(void) { return next_left != 0; }
