/* ovl.c — オーバレイの読み込み(ovl.h 参照)。 */
#include "ovl.h"
#include "rom.h"

u8 g_ovl, g_ovl_ready;
static u8 load_id, init_pending;
static u16 load_off;

#define BANK_OF(id) ((u8)(OVL_BANK0 + ((id) - 1) * 2))

void ovl_load_sync(u8 id) {
    g_ovl_ready = 0; load_id = 0; init_pending = 0;
    rom_to_ram(BANK_OF(id), 0, (u8 *)OVL_BASE, OVL_SIZE);
    g_ovl = id; g_ovl_ready = 1;
}

void ovl_request(u8 id) { g_ovl_ready = 0; g_ovl = OVL_NONE; load_id = id; load_off = 0; init_pending = 0; }

/* 1 フレームに 512 バイト(ROM から RAM へ)。読み終えた次のフレームで面の ovl_init。
   ★1KB ずつ読んで同じフレームで ovl_init まで済ませると、空中戦の入りのフレームが遅れた */
void ovl_step(void) {
    u16 n;
    if (init_pending) {
        init_pending = 0; g_ovl_ready = 1;
        ovl_init();
        return;
    }
    if (!load_id) return;
    n = (u16)(OVL_SIZE - load_off) < 512u ? (u16)(OVL_SIZE - load_off) : 512u;
    rom_to_ram(BANK_OF(load_id), load_off, (u8 *)(OVL_BASE + load_off), n);
    load_off += n;
    if (load_off >= OVL_SIZE) {
        g_ovl = load_id; load_id = 0;
        if (g_ovl >= OVL_S1) init_pending = 1; else g_ovl_ready = 1;
    }
}
