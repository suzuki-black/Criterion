/* rom.c — ROM のデータバンクを読む(rom.h 参照)。
   起動時にコード(ページ 1-2)を RAM へ複製して RAM から実行しているので(sys_ram_exec)、ROM のデータを読むときだけ
   ページ 1-2 をカートリッジへ戻し、ASCII8 の 0x8000-0xBFFF の窓(バンクレジスタ 0x7000 / 0x7800)にデータの
   バンクを出して写す。
   ★この関数(と割込み処理 raster.c)はページ 1(0x4000-0x7FFF)に置く(Makefile が検査)。ページ 1 のカートリッジの
     中身は RAM の写しと同じ(バンク 0/1 は変えない)なので、切り替えても実行は途切れず、割込みも止めずに済む。
   ★写している間はページ 2 が別物になる。割込み処理からページ 2 のもの(コード・const データ)を触らないこと
     (PCM の試験音のような const データがページ 2 にあると、その間は別のバイトを鳴らす)。 */
#include "rom.h"
#include "vdp.h"

extern u8 ramx_a8;                   /* sys.c: 起動時の基本スロット選択(ページ 1-2 = カートリッジ) */

/* asm への受け渡し */
u8  rom_bank;
u16 rom_src;                         /* 0x8000 + 窓の中の位置 */
u16 rom_len;
u8 *rom_dst;                         /* RAM へ写すとき(ページ 0 か 3) */

/* rom_len バイトを VRAM(アドレスは設定済み)へ。rom_dst が 0 以外なら RAM へ。 */
void rom_copy(void) __naked {
    __asm
        in   a, (0xA8)
        push af                    ; 今の選択(ページ 0 は RAM、ページ 3 は RAM)
        ld   b, a
        ld   a, (_ramx_a8)
        and  #0x3C                 ; ページ 1-2 = カートリッジ
        ld   c, a
        ld   a, b
        and  #0xC3
        or   c
        out  (0xA8), a             ; ★ここから先、ページ 1 はカートリッジの同じコード
        ld   a, (_rom_bank)
        ld   (0x7000), a           ; 0x8000-0x9FFF
        inc  a
        ld   (0x7800), a           ; 0xA000-0xBFFF
        ld   hl, (_rom_src)
        ld   de, (_rom_len)
        ld   bc, (_rom_dst)
        ld   a, b
        or   c
        jr   nz, 00010$
        ; -- VRAM へ: OTIR を 256 バイトずつ --
        ld   c, #0x98
    00001$:
        ld   a, d
        or   a
        jr   z, 00002$
        ld   b, #0
        otir                       ; 256 バイト
        dec  d
        jr   00001$
    00002$:
        ld   a, e
        or   a
        jr   z, 00020$
        ld   b, a
        otir
        jr   00020$
        ; -- RAM へ --
    00010$:
        ld   a, d
        or   e
        jr   z, 00020$
        push de
        ld   d, b
        ld   e, c                  ; DE = 写し先
        pop  bc                    ; BC = バイト数
        ldir
    00020$:
        ld   a, #2
        ld   (0x7000), a           ; 窓をコードのバンクへ戻す
        inc  a
        ld   (0x7800), a
        pop  af
        out  (0xA8), a             ; ページ 1-2 を RAM へ
        ret
    __endasm;
}

/* ROM の (bank, off) から len バイト。窓(16KB)と VRAM の 16KB 境界(アドレスの桁上げが R#14 へ伝わらない)で分ける。 */
void rom_to_vram(u8 bank, u16 off, u8 vhi, u16 vlo, u16 len) {
    while (len) {
        u16 chunk, room;
        bank = (u8)(bank + (off >> 13));
        off &= 0x1FFF;
        chunk = (u16)(0x4000u - off);                      /* 窓に入る分 */
        room = (u16)(0x4000u - (vlo & 0x3FFF));            /* VRAM の 16KB 境界まで */
        if (chunk > room) chunk = room;
        if (chunk > len) chunk = len;
        vdp_write_addr17(vhi, vlo);
        rom_bank = bank; rom_src = (u16)(0x8000u + off); rom_len = chunk; rom_dst = 0;
        rom_copy();
        len -= chunk;
        off += chunk;
        if ((u16)(vlo + chunk) < vlo) vhi++;
        vlo += chunk;
    }
}

void rom_to_ram(u8 bank, u16 off, u8 *dst, u16 len) {
    while (len) {
        u16 chunk;
        bank = (u8)(bank + (off >> 13));
        off &= 0x1FFF;
        chunk = (u16)(0x4000u - off);
        if (chunk > len) chunk = len;
        rom_bank = bank; rom_src = (u16)(0x8000u + off); rom_len = chunk; rom_dst = dst;
        rom_copy();
        len -= chunk; off += chunk; dst += chunk;
    }
}
