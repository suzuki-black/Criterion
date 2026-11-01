/* zoom.c — 近づいてくる大きな物(2 面の KILOGRAM・5 面の遠くの戦艦)の絵を ROM から流す。
   大きさごとの絵を Python で描いて ROM に並べ(1 枚 = 見出し 4 バイト + 絵)、1 枚ずつ VRAM の作業の場所へ写して、揃ったら呼び出し側が
   HMMM で表示へ写す。VRAM に置いておける段は数が限られる(大きな段を 8 枚で段差が目立った。指摘)が、ROM には空きがあり、
   1 枚ずつ流すなら VRAM は 1 枚ぶんで足りる。
   ★写し終わる前に次の絵を書き始めないよう、前の HMMM が出て 3 フレーム待ってから読む(作業の場所を共有している。96x96 で約 1.4 フレーム) */
#include "game.h"
#include "ghost.h"
#include "rom.h"
#include "sys.h"

extern u16 g_frame_t0;

#define ROW_BYTES 400u                /* 1 フレームに写す量(R800 の VDP への I/O は 1 バイト 8.6us) */
enum { Z_OFF, Z_HEAD, Z_ROWS, Z_WAIT, Z_DONE };

u8 g_zm_w, g_zm_h, g_zm_done;
static u8  st, bank, hold, last, t, row;
static u16 off, vbase, va;

void zoom_start(u8 b, u16 o, u16 vaddr) {
    bank = b; off = o; vbase = vaddr;
    st = Z_HEAD; t = 3; g_zm_done = 0;
}

u8 zoom_step(void) {
    u8 hdr[4];
    u16 n = 0;
    if (st == Z_OFF || st == Z_DONE) return 0;
    t++;
    if (st == Z_HEAD) {
        if (gq_count || t < 3) return 0;
        rom_to_ram(bank, off, hdr, 4);
        off += 4;
        bank = (u8)(bank + (off >> 13)); off &= 0x1FFF;
        g_zm_w = hdr[0]; g_zm_h = hdr[1]; hold = hdr[2]; last = hdr[3];
        row = 0; va = vbase; st = Z_ROWS;
    }
    if (st == Z_ROWS) {
        while (row < g_zm_h && n < ROW_BYTES && (u16)(sys_timer() - g_frame_t0) < 170u * 16u) {
            rom_to_vram(bank, off, 1, va, g_zm_w);
            off += g_zm_w; bank = (u8)(bank + (off >> 13)); off &= 0x1FFF;
            va += 128; row++; n += g_zm_w;
        }
        if (row < g_zm_h) return 0;
        st = Z_WAIT;
    }
    if (t < hold) return 0;                /* 見出しの「保つ」= 前の 1 枚からの間 */
    t = 0;
    st = last ? Z_DONE : Z_HEAD;
    g_zm_done = last;
    return 1;
}
