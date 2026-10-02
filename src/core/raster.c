/* raster.c — ラスターリスト(raster.h 参照)。
   出典: ZeroNoHoukouR/src/core/raster.c(H.KEYI フック・S#1 FH 判定・VBLANK での仕切り直し)を、
   ダブルバッファの表・HR ポーリング帯・直接割込み(ページ 0 RAM)に拡張。
   ★asm から参照する変数・関数は非 static(static だと参照が消える/名前が変わる)。 */
#include "raster.h"
#include "vdp.h"

RasEntry ras_buf0[RAS_MAX];
RasEntry ras_buf1[RAS_MAX];
RasEntry *volatile ras_front = ras_buf0;   /* ISR が読む表 */
RasEntry *volatile ras_backp = ras_buf1;   /* メインが組む表 */
volatile u8 ras_front_n;
volatile u8 ras_back_n;
volatile u8 ras_swap;          /* 1 = 次の VBLANK で表を差し替える */
RasEntry *volatile ras_ptr;    /* 次に適用する項目 */
volatile u8 ras_left;          /* 残り項目数 */
volatile u8 ras_mode_now;      /* ISR 内での入口の種別(1=直接) */
volatile u16 g_ras_irqs;       /* 割込みの総数(計測用) */
volatile u16 g_ras_fh;         /* うち FH(走査線)の数 */
volatile u16 g_ras_vb;         /* うち VBLANK の数 */

volatile u8 g_ras_bias = 4;   /* openMSX(FS-A1ST)で境界のずれが 0 になる値。実機で取り直す */
volatile u8 g_ras_busy_border;

static u8 isr_mode;
static u8 saved_a8, saved_sub, sub_used;

/* ---- 割込みハンドラ(ROM 上。M1 で RAM 実行へ移す) ----
   入口 2 つ。直接方式は S#0 も読んで VBLANK フラグを落とし、JIFFY を進め、ei; ret で戻る。
   BIOS 方式は FH でなければ VBLANK とみなし、ret で BIOS へ戻す(BIOS が S#0 を読んで H.TIMI を呼ぶ)。 */
void ras_isr_direct(void) __naked {
    __asm
        push af
        ld   a, #1
        jr   ras_common
    __endasm;
}

void ras_isr_bios(void) __naked {
    __asm
        push af
        xor  a
    ras_common:
        push bc
        push de
        push hl
        push ix
        ld   (_ras_mode_now), a
        ld   hl, (_g_ras_irqs)
        inc  hl
        ld   (_g_ras_irqs), hl
        ld   a, (_g_ras_busy_border)
        or   a
        jr   z, 00001$
        out  (0x99), a
        ld   a, #0x87              ; R#7 = 枠線色(ISR 中)
        out  (0x99), a
    00001$:
        ld   a, #1
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 1
        out  (0x99), a
        in   a, (0x99)             ; S#1(読むと FH が落ちる)
        ld   b, a
        xor  a
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 0
        out  (0x99), a
        ld   a, (_ras_mode_now)
        or   a
        jr   z, 00002$
        in   a, (0x99)             ; 直接方式: S#0(読むと VBLANK フラグが落ちる)
        ld   c, a
        jr   00003$
    00002$:
        ld   c, #0
        bit  0, b
        jr   nz, 00003$
        ld   c, #0x80              ; BIOS 方式: FH でなければ VBLANK とみなす
    00003$:
        bit  0, b
        jr   z, 00004$
        ld   hl, (_g_ras_fh)
        inc  hl
        ld   (_g_ras_fh), hl
        ld   a, (_ras_left)
        or   a
        call nz, _ras_apply_next
    00004$:
        bit  7, c
        jr   z, 00006$
        ld   hl, (_g_ras_vb)
        inc  hl
        ld   (_g_ras_vb), hl
        ld   a, (_ras_mode_now)
        or   a
        jr   z, 00005$
        ld   hl, (0xFC9E)          ; 直接方式は BIOS の代わりに JIFFY を進める
        inc  hl
        ld   (0xFC9E), hl
    00005$:
        call _ras_vblank
    00006$:
        ld   a, (_g_ras_busy_border)
        or   a
        jr   z, 00007$
        xor  a
        out  (0x99), a
        ld   a, #0x87              ; R#7 = 0
        out  (0x99), a
    00007$:
        pop  ix
        pop  hl
        pop  de
        pop  bc
        ld   a, (_ras_mode_now)
        or   a
        jr   nz, 00008$
        pop  af
        ret
    00008$:
        pop  af
        ei
        ret
    __endasm;
}

/* 項目 1 つを適用して次の分割行を仕込む。★C(呼び出し元の S#0)は壊さない。 */
void ras_apply_next(void) __naked {
    __asm
        ld   ix, (_ras_ptr)
        ld   d, 1 (ix)             ; flags
        bit  0, d
        jr   z, 00001$
        ld   a, 2 (ix)
        out  (0x99), a
        ld   a, #0x9A              ; R#26
        out  (0x99), a
        ld   a, 3 (ix)
        out  (0x99), a
        ld   a, #0x9B              ; R#27
        out  (0x99), a
    00001$:
        bit  1, d
        jr   z, 00002$
        ld   a, 4 (ix)
        out  (0x99), a
        ld   a, #0x90              ; R#16 = パレット番号
        out  (0x99), a
        ld   a, 5 (ix)
        out  (0x9A), a
        ld   a, 6 (ix)
        out  (0x9A), a
    00002$:
        bit  2, d
        jr   z, 00005$
        ; ---- HR ポーリング帯 ----
        ; 帰線期間(HR=1)に入ってからの書き込みを最小にするため、R#17(間接レジスタ番号)は
        ; 表示期間のうちに設定し、帰線では 0x9B への OUT だけを行う。
        ;   RAS_HAZE_FINE: R#27 だけを毎ライン(OUT 1 回。R#17 = 27 | 0x80 で自動加算なし)
        ;   それ以外      : R#26, R#27 を毎ライン(OUT 2 回。R#17 = 26 を毎ライン設定し直す)
        push bc                    ; ★C は呼び出し元の S#0(bit7 = VBLANK)。outi 用に C を使うので退避する
                                   ;   (退避し忘れて C=0x9B のまま戻り、陽炎帯の終わりを毎フレーム VBLANK と誤判定していた)
        ld   e, 7 (ix)             ; ライン数
        ld   l, 8 (ix)
        ld   h, 9 (ix)             ; (r26, r27) 表
        ld   c, #0x9B
        ld   a, #2
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 2(S#2: HR を見る)
        out  (0x99), a
        bit  3, d
        jr   z, 00010$
        ; -- R#27 のみ --
        ld   a, #0x80 | 27
        out  (0x99), a
        ld   a, #0x80 | 17
        out  (0x99), a
        inc  hl                    ; r27 の位置へ
    00006$:
        in   a, (0x99)
        and  #0x20
        jr   nz, 00006$            ; 表示期間に入るのを待つ
    00007$:
        in   a, (0x99)
        and  #0x20
        jr   z, 00007$             ; 帰線(HR=1)
        outi                       ; R#27 ← (hl)(B は使わない)
        inc  hl                    ; 次の行の r27
        dec  e
        jr   nz, 00006$
        jr   00012$
        ; -- R#26 + R#27 --
    00010$:
        ld   a, #26
        out  (0x99), a
        ld   a, #0x80 | 17         ; R#17 = 26(自動加算あり)。表示期間中に設定しておく
        out  (0x99), a
    00003$:
        in   a, (0x99)
        and  #0x20
        jr   nz, 00003$
    00004$:
        in   a, (0x99)
        and  #0x20
        jr   z, 00004$
        outi                       ; R#26
        outi                       ; R#27
        dec  e
        jr   nz, 00010$
    00012$:
        xor  a
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 0
        out  (0x99), a
        pop  bc
    00005$:
        ld   de, #10               ; sizeof(RasEntry)
        add  ix, de
        ld   (_ras_ptr), ix
        ld   hl, #_ras_left
        dec  (hl)
        ret  z
        ld   a, (_g_ras_bias)
        ld   b, a
        ld   a, 0 (ix)
        sub  b
        out  (0x99), a
        ld   a, #0x93              ; R#19 = 次の分割行
        out  (0x99), a
        ret
    __endasm;
}

/* VBLANK: 表の差し替え → 先頭(line==0)の適用 → 最初の分割行の仕込み。 */
void ras_vblank(void) __naked {
    __asm
        ld   a, (_ras_swap)
        or   a
        jr   z, 00001$
        xor  a
        ld   (_ras_swap), a
        ld   hl, (_ras_front)
        ld   de, (_ras_backp)
        ld   (_ras_front), de
        ld   (_ras_backp), hl
        ld   a, (_ras_back_n)
        ld   (_ras_front_n), a
    00001$:
        ld   hl, (_ras_front)
        ld   (_ras_ptr), hl
        ld   a, (_ras_front_n)
        ld   (_ras_left), a
        or   a
        ret  z
        ld   ix, (_ras_ptr)
        ld   a, 0 (ix)
        or   a
        jp   z, _ras_apply_next    ; line==0 は今ここで適用(次の分割行も仕込まれる)
        ld   b, a
        ld   a, (_g_ras_bias)
        ld   d, a
        ld   a, b
        sub  d
        out  (0x99), a
        ld   a, #0x93
        out  (0x99), a
        ret
    __endasm;
}

RasEntry *ras_back(void) {
    __asm di __endasm;
    ras_swap = 0;                  /* 確定前の表を VBLANK に拾わせない */
    __asm ei __endasm;
    return ras_backp;
}

u8 ras_back_index(void) {
    return ras_backp == ras_buf1;
}

void ras_commit(u8 n) {
    __asm di __endasm;
    ras_back_n = n;
    ras_swap = 1;
    __asm ei __endasm;
}

void ras_init(void) {
    volatile u8 *h = (volatile u8 *)0xFD9A;     /* H.KEYI */
    volatile u8 *rg0 = (volatile u8 *)0xF3DF;   /* RG0SAV */
    ras_front_n = ras_back_n = ras_left = 0;
    isr_mode = RAS_ISR_BIOS;
    __asm di __endasm;
    h[1] = (u8)(u16)ras_isr_bios;
    h[2] = (u8)((u16)ras_isr_bios >> 8);
    h[0] = 0xC3;
    *rg0 |= 0x10;                               /* IE1(走査線割込み)を有効に */
    VDP_CTRL = *rg0;
    VDP_CTRL = 0x80;
    __asm ei __endasm;
}

/* ページ 0 を RAM にする。RAM のスロットはページ 3 と同じ(ページ 3 は常に RAM)。
   拡張スロットなら 2 次スロットレジスタ(0xFFFF、読むと反転値)のページ 0 ビットも合わせる。
   ★0xFFFF はページ 3 にいるスロットの 2 次レジスタなので、ページ 3 と同じスロットを選ぶ限り安全。 */
static void page0_ram(void) {
    __asm
        di
        in   a, (0xA8)
        ld   (_saved_a8), a
        rlca
        rlca
        and  #3                    ; A = ページ 3 の基本スロット
        ld   c, a
        ld   hl, #0xFCC1           ; EXPTBL
        ld   e, a
        ld   d, #0
        add  hl, de
        bit  7, (hl)
        ld   a, #0
        ld   (_sub_used), a
        jr   z, 00001$
        ld   a, (0xFFFF)
        cpl                        ; 現在の 2 次スロット選択
        ld   (_saved_sub), a
        ld   b, a
        rlca
        rlca
        and  #3                    ; ページ 3 の 2 次スロット
        ld   d, a
        ld   a, b
        and  #0xFC
        or   d
        ld   (0xFFFF), a
        ld   a, #1
        ld   (_sub_used), a
    00001$:
        ld   a, (_saved_a8)
        and  #0xFC
        or   c
        out  (0xA8), a
    __endasm;
}

static void page0_bios(void) {
    __asm
        di
        ld   a, (_saved_a8)
        out  (0xA8), a             ; 先にページ 0 を BIOS のスロットへ戻す
        ld   a, (_sub_used)
        or   a
        jr   z, 00001$
        ld   a, (_saved_sub)
        ld   (0xFFFF), a
    00001$:
    __endasm;
}

u8 ras_set_isr(u8 mode) {
    if (mode == isr_mode) return isr_mode;
    if (mode == RAS_ISR_DIRECT) {
        page0_ram();               /* di のまま戻る */
        *(volatile u8 *)0x0038 = 0xC3;
        *(volatile u8 *)0x0039 = (u8)(u16)ras_isr_direct;
        *(volatile u8 *)0x003A = (u8)((u16)ras_isr_direct >> 8);
        if (*(volatile u8 *)0x0038 != 0xC3) {   /* RAM にならなかった: BIOS へ戻す */
            page0_bios();
            __asm ei __endasm;
            return isr_mode;
        }
    } else {
        page0_bios();
    }
    isr_mode = mode;
    __asm ei __endasm;
    return isr_mode;
}
