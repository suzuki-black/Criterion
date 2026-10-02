/* vdp.c — V9958 アクセスの最小層(vdp.h 参照)。 */
#include "vdp.h"

void vdp_wreg(u8 r, u8 v) {
    __asm di __endasm;
    VDP_CTRL = v;
    VDP_CTRL = 0x80 | r;
    __asm ei __endasm;
}

void vdp_set_pal(u8 idx, u8 r, u8 g, u8 b) {
    __asm di __endasm;
    VDP_CTRL = idx;
    VDP_CTRL = 0x80 | 16;
    VDP_PAL  = (u8)((r << 4) | b);
    VDP_PAL  = g;
    __asm ei __endasm;
}

void vdp_write_addr17(u8 hi, u16 lo) {
    __asm di __endasm;
    VDP_CTRL = (u8)((hi << 2) | (lo >> 14));  VDP_CTRL = 0x80 | 14;
    VDP_CTRL = (u8)(lo & 0xFF);               VDP_CTRL = (u8)(((lo >> 8) & 0x3F) | 0x40);
    __asm ei __endasm;
}

/* CE(S#2 bit0) のポーリング。S#2 選択中に VBLANK 割込みが入ると BIOS の ISR が S#0 を読めず
   永久再突入するので、1 回ごとに di で囲んで必ず待機値(BIOS 方式は S#0)へ戻す(前作で実機確定)。
   ★rra の後に ld a,(nn) を使う(xor a は C フラグを壊す)。 */
void (*volatile g_vdp_wait_hook)(void);
volatile u8 g_vdp_status_rest;   /* R#15 の待機値(S#2 を読んだ後に戻す値) */

void vdp_cmd_wait(void) {
    __asm
        jr   00002$
    00003$:
        ld   hl, (_g_vdp_wait_hook)
        ld   a, h
        or   l
        call nz, ___sdcc_call_hl   ; 待ちの合間のポーリング(PCM 方式 B)
    00002$:
        di
        ld   a, #2
        out  (0x99), a
        ld   a, #0x8F
        out  (0x99), a
        in   a, (0x99)
        rra
        ld   a, (_g_vdp_status_rest)
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 待機値(直接割込みでは 1)
        out  (0x99), a
        ei
        jp   c, 00003$
    __endasm;
}

/* R#32..46 の 15 本。R#17 間接オートインクリメント + OTIR(0x9B は 0x99 のラッチを使わない)。
   ★5 本ずつ 3 回に分け、割込み禁止を 1 回 7 アクセス(約 60us、1 ライン弱)に抑える。turboR の R800 は
     VDP への I/O 1 回に 8.6us かかるので、15 本を一度に書くと 2.3 ライン割込みが止まり、2 ラインごとの
     PCM 割込み(raster.c)を毎回取りこぼした(M0-3)。割込み側も R#17 を使う(陽炎帯)ので、回ごとに設定し直す。
     R#46(CMD)は最後に書かれるので、途中で割り込まれてもコマンドは揃ってから始まる。 */
volatile u8 vdpcbuf[15];

static void cmd_flush(void) {
    __asm
        ld   hl, #_vdpcbuf
        ld   c, #0x9B
        ld   e, #32
    00001$:
        di
        ld   a, e
        out  (0x99), a
        ld   a, #0x80 | 17         ; R#17 = 次の番号(自動加算あり)
        out  (0x99), a
        ld   b, #5
        otir
        ei
        ld   a, e
        add  a, #5
        ld   e, a
        cp   #47
        jr   c, 00001$
    __endasm;
}

static void cmd_rect(u16 dx, u16 dy, u16 nx, u16 ny) {
    vdpcbuf[4]  = (u8)dx; vdpcbuf[5]  = (u8)((dx >> 8) & 0x01);
    vdpcbuf[6]  = (u8)dy; vdpcbuf[7]  = (u8)((dy >> 8) & 0x03);
    vdpcbuf[8]  = (u8)nx; vdpcbuf[9]  = (u8)((nx >> 8) & 0x01);
    vdpcbuf[10] = (u8)ny; vdpcbuf[11] = (u8)((ny >> 8) & 0x03);
    vdpcbuf[13] = 0;      /* ARG: 左上→右下 */
}

void vdp_hmmm(u16 sx, u16 sy, u16 dx, u16 dy, u16 nx, u16 ny) {
    vdp_cmd_wait();
    vdpcbuf[0] = (u8)sx; vdpcbuf[1] = (u8)((sx >> 8) & 0x01);
    vdpcbuf[2] = (u8)sy; vdpcbuf[3] = (u8)((sy >> 8) & 0x03);
    cmd_rect(dx, dy, nx, ny);
    vdpcbuf[14] = 0xD0;
    cmd_flush();
}

void vdp_hmmv(u16 dx, u16 dy, u16 nx, u16 ny, u8 byte) {
    vdp_cmd_wait();
    cmd_rect(dx, dy, nx, ny);
    vdpcbuf[12] = byte;
    vdpcbuf[14] = 0xC0;
    cmd_flush();
}

/* n バイト(0 = 256)を VRAM へ続けて書く(vdp_write_addr17 の後)。OTIR で R800 の命令の負担を最小にする
   (速さは VDP の I/O 待ち 8.6us/バイトで決まる)。引数は A = n、DE = p(SDCC の sdcccall(1))。 */
void vdp_out_block(u8 n, const u8 *p) __naked {
    (void)n; (void)p;
    __asm
        ld   b, a
        ex   de, hl
        ld   c, #0x98
        otir
        ret
    __endasm;
}
