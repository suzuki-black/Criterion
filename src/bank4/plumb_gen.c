/* plumb_gen.c — 1 面のボス PLUMB の絵を作る(第 4 バンク。2026-10-29 に 1 面のオーバレイ(boss.c)から移した。オーバレイの空きを作るため)。
   傾き 7 枚(±34 度)と縮小 3 枚を ROTOR で 1 枚ずつ作り、ページ 2 の枠(4 枚 × 3 段、64x64)に置く。枠の並びは gen_boss.py の
   FRAMES と同じ(boss.c の rg_dx / rg_dy / fr_r もこの並び)。
   1 回の呼び出しは 1 段: まず元の絵(2KB)を 512 バイトずつ、次に 1 枚につき 0..15 = 4 行ずつ計算(約 20 ライン)、
   16..23 = 8 行ずつ VRAM へ(256 バイト、約 36 ライン)。★8 行ずつ計算すると、敵の多いフレームと重なって遅れた */
#include "game.h"
#include "vdp.h"
#include "rom.h"
#include "rotor.h"
#include "assets.h"

#define NFRAME 10
static const s8  fr_angle[NFRAME] = { -6, -4, -2, 0, 2, 4, 6, 0, 0, 0 };
static const u16 fr_step[NFRAME]  = { 256, 256, 256, 256, 256, 256, 256, 301, 366, 465 };
static u8  gen_k, gen_ph;          /* 作っている枠と段(gen_k == NFRAME で終わり) */
static u16 src_off;                /* 元の絵を ROM から読んだバイト数 */

/* reset = 1 で作り始める(0 を返す)。以後は 1 段ずつ進め、全部できたら 1 を返す。★変数は初期化されないので reset で全部を代入する */
u8 plumb_gen(u8 reset) {
    if (reset) { gen_k = 0; gen_ph = 0; src_off = 0; return 0; }
    if (gen_k >= NFRAME) return 1;
    if (src_off < ASSET_BOB_LEN) {                            /* まず元の絵(2KB)を 4 段で */
        rom_to_ram(ASSET_BOB_BANK, (u16)(ASSET_BOB_OFF + src_off), ROTOR_SRC + src_off, 512);
        src_off += 512;
        return 0;
    }
    if (gen_ph < 16) {
        if (gen_ph == 0) rotor_begin(64, (u8)fr_angle[gen_k], fr_step[gen_k], 1);
        rotor_rows(ROTOR_DST + gen_ph * 128u, 4);
    } else {
        u8 r, r0 = (u8)((gen_ph - 16) * 8);
        for (r = r0; r < r0 + 8; r++) {
            vdp_write_addr17(1, (u16)(((gen_k >> 2) * 64u + r) * 128u + (gen_k & 3) * 32u));
            vdp_out_block(32, ROTOR_DST + r * 32u);
        }
    }
    if (++gen_ph >= 24) { gen_ph = 0; gen_k++; }
    return gen_k >= NFRAME;
}
