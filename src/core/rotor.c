/* rotor.c — 回転拡縮エンジン ROTOR(rotor.h 参照)。
   写し先の各ドット (x, y) から元の位置 (u, v) を逆にたどる(穴の空かないアフィン写像)。
     u = 32 + step・( cos・(x - n/2) + sin・(y - n/2))
     v = 32 + step・(-sin・(x - n/2) + cos・(y - n/2))
   行ごとの始点を C で求め、1 行の中は asm で u, v に一定の増分を足すだけ(掛け算なし)。u, v は 8.8 固定小数点。
   ★asm は IX = u、IY = v、裏レジスタの BC' / DE' に増分を持つ(割込み処理は裏レジスタを使わない約束)。
     sdasz80 は IXh / IYh を書けないので命令のバイトを直に置く(R800 は正式に持つ命令)。 */
#include "rotor.h"

extern const s8 g_sin64[64];

/* asm への受け渡し */
s16 rot_u, rot_v, rot_dux, rot_dvx;
u8 *rot_dst;
u8  rot_pairs, rot_bg;

/* 1 行(rot_pairs × 2 ドット)を rot_dst へ */
static void rotor_row(void) __naked {
    __asm
        push ix
        push iy
        ld   ix, (_rot_u)
        ld   iy, (_rot_v)
        ld   de, (_rot_dst)
        exx
        ld   bc, (_rot_dux)
        ld   de, (_rot_dvx)
        exx
        ld   a, (_rot_pairs)
        ld   b, a
    00001$:
        call 00010$                ; 左のドット(上位 4 ビット)
        rlca
        rlca
        rlca
        rlca
        ld   c, a
        call 00010$                ; 右のドット
        or   c
        ld   (de), a
        inc  de
        djnz 00001$
        pop  iy
        pop  ix
        ret
        ; ---- (u, v) の色を A へ。それから u += dux、v += dvx。B, C, DE は壊さない ----
    00010$:
        .db  0xFD, 0x7C            ; ld a, iyh(v の整数部)
        cp   #64
        jr   nc, 00020$            ; 外(負は 0x80 以上)
        ld   h, a
        .db  0xDD, 0x7C            ; ld a, ixh(u の整数部)
        cp   #64
        jr   nc, 00020$
        srl  a
        ld   l, a                  ; L = u / 2(0..31)
        ld   a, h
        rrca
        rrca
        rrca                       ; 上位 3 ビット = (v & 7) << 5、下位 3 ビット = v >> 3
        ld   h, a
        and  #0xE0
        or   l
        ld   l, a
        ld   a, h
        and  #0x07
        add  a, #0xD8              ; ROTOR_SRC の上位(rotor.h)
        ld   h, a
        ld   h, (hl)               ; 2 ドット分のバイト
        .db  0xDD, 0x7C            ; ld a, ixh
        rra                        ; CF = u & 1(奇数は下位 4 ビット)
        ld   a, h
        jr   c, 00011$
        rrca
        rrca
        rrca
        rrca
    00011$:
        and  #0x0F
        jr   00021$
    00020$:
        ld   a, (_rot_bg)
    00021$:
        exx
        add  ix, bc
        add  iy, de
        exx
        ret
    __endasm;
}

/* 符号つき 8 ビット × 16 ビット → >> 7(倍率つきの三角関数) */
static s16 mul7(s8 a, u16 step) { return (s16)(((s32)a * (s32)step) >> 7); }

static s16 cur_u, cur_v, duy, dvy;
static u8 cur_n;

void rotor_begin(u8 n, u8 angle, u16 step, u8 bg) {
    s8 c = g_sin64[(u8)(angle + 16) & 63], s = g_sin64[angle & 63];
    s16 dux = mul7(c, step), dvx = (s16)-mul7(s, step);      /* 写し先の x が 1 進むとき */
    s16 h = (s16)(n >> 1);
    duy = mul7(s, step); dvy = mul7(c, step);                 /* 写し先の y が 1 進むとき */
    cur_u = (s16)(0x2080 - dux * h - duy * h);               /* (0, 0) のドットの中心 */
    cur_v = (s16)(0x2080 - dvx * h - dvy * h);
    rot_dux = dux; rot_dvx = dvx; rot_bg = bg; rot_pairs = (u8)(n >> 1); cur_n = n;
}

void rotor_rows(u8 *dst, u8 rows) {
    while (rows--) {
        rot_u = cur_u; rot_v = cur_v; rot_dst = dst;
        rotor_row();
        cur_u += duy; cur_v += dvy; dst += cur_n >> 1;
    }
}

void rotor_4bpp(u8 *dst, u8 n, u8 angle, u16 step, u8 bg) {
    rotor_begin(n, angle, step, bg);
    rotor_rows(dst, n);
}
