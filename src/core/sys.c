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

/* ---- RAM 実行(指示書 8.1「毎フレーム走るコードは RAM に転送して実行する」) ----
   R800 はカートリッジ ROM からの命令読み出しが遅い(前作の実機実測で RAM の約 3.8 倍)。
   ページ 1-2(0x4000-0xBFFF)を RAM のマッパーセグメント 4/5 へ複製し、ページ 1-2 を RAM のスロットへ
   切り替える。切替の瞬間も同じ番地に同じコードがあるので、ページ 1 から実行したまま切り替えられる。
   ★複製の窓にはページ 0 を使う(RAM スロット + セグメント 4/5 を順に見せる)。その間は di。
   ★以後カートリッジ ROM は見えない(バンク切替を使う M1 では前作の動的切替 ramexec にする)。
   ★RAM のスロットはページ 3 と同じ。拡張スロットなら 0xFFFF(読むと反転)のページ 0-2 も合わせる。
   ★カートリッジが RAM と同じ基本スロットにある構成では行わない(戻り値 0)。 */
#define RAMX_SEG1 4
#define RAMX_SEG2 5

static const u8 ramx_probe = 0x5A;   /* 複製後に書き換えて RAM になったかを確かめる */
u8 ramx_a8;                          /* 元の基本スロット選択(asm から参照) */
u8 ramx_did;                         /* 1 = 切り替えた */

u8 sys_ram_exec(void) {
    u8 ok;
    __asm
        di
        xor  a
        ld   (_ramx_did), a
        in   a, (0xA8)
        ld   (_ramx_a8), a
        rlca
        rlca
        and  #3
        ld   c, a                  ; C = RAM の基本スロット(ページ 3)
        ld   a, (_ramx_a8)
        rrca
        rrca
        and  #3                    ; ページ 1(カートリッジ)の基本スロット
        cp   c
        jr   z, 00009$             ; 同じ基本スロット: 行わない
        ld   hl, #0xFCC1           ; EXPTBL
        ld   e, c
        ld   d, #0
        add  hl, de
        bit  7, (hl)
        jr   z, 00001$
        ld   a, (0xFFFF)
        cpl
        ld   d, a                  ; D = 現在の 2 次スロット選択
        rlca
        rlca
        and  #3                    ; ページ 3 の 2 次スロット
        ld   e, a
        rlca
        rlca
        or   e
        rlca
        rlca
        or   e
        ld   e, a                  ; ページ 0-2 に同じ 2 次スロット
        ld   a, d
        and  #0xC0
        or   e
        ld   (0xFFFF), a
    00001$:
        ; --- ページ 0 を RAM にして複製(C は ldir で壊れるので先に A8 の値を作っておく) ---
        ld   a, (_ramx_a8)
        and  #0xFC
        or   c
        ld   d, a                  ; D = ページ 0 だけ RAM
        ld   a, c
        rlca
        rlca
        or   c
        rlca
        rlca
        ld   e, a                  ; E = ページ 1-2 の RAM ビット(bit5-2)
        ld   a, d
        out  (0xA8), a
        push de
        ld   a, #RAMX_SEG1
        out  (0xFC), a
        ld   hl, #0x4000
        ld   de, #0x0000
        ld   bc, #0x4000
        ldir
        ld   a, #RAMX_SEG2
        out  (0xFC), a
        ld   hl, #0x8000
        ld   de, #0x0000
        ld   bc, #0x4000
        ldir
        ld   a, #3
        out  (0xFC), a             ; ページ 0 のセグメントを既定(3)へ
        pop  de
        ; --- ページ 1-2 を RAM へ、ページ 0 は BIOS へ戻す(内容は同一なので実行は途切れない) ---
        ld   a, #RAMX_SEG1
        out  (0xFD), a
        ld   a, #RAMX_SEG2
        out  (0xFE), a
        ld   a, (_ramx_a8)
        and  #0xC3                 ; ページ 0 と 3 は元のまま
        or   e
        out  (0xA8), a
        ld   a, #1
        ld   (_ramx_did), a
    00009$:
        ei
    __endasm;
    /* ★切り替えていないときに書くと ROM(0x6000-0x7FFF はマッパーのバンクレジスタ)を叩くので、
       切り替えたときだけ確かめる。 */
    if (!ramx_did) return 0;
    {
        volatile u8 *p = (volatile u8 *)&ramx_probe;
        __asm di __endasm;
        *p = 0xA5;
        ok = (*p == 0xA5);
        *p = 0x5A;
        __asm ei __endasm;
    }
    return ok;
}
