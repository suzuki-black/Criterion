/* farz.c — 5 面の接近: 遠くの戦艦が近づいて、本編の艦首そのものになるまで(第 4 バンク。5 面のオーバレイに空きが無い)。
   絵は tools/gen_stage5.py の far_stream(本編の短冊・主砲塔・機銃・艦橋の塔を縮めたもの)。1 段 = 1 つ前の段との差分で、
   縁が 1 段に 1 ドットまでしか動かないように刻んである。それを FZ_STEP フレームごとに 1 段ずつ、表示リングの見えている所へ
   直に書く(書きかけが見えても 1 ドットの違いなので目立たない)。艦首の左右の動きは絵に入れず、段ごとの d(farz_data.h)を
   s5.c が空の帯の横スクロールに足す。
   記録 = 行(0..105)・バイトの位置・長さ(最上位ビット = 同じバイトのくり返し。中身は 1 バイト)・中身。
   0xFE = 次の 256 バイトの塊へ、0xFF = 段の終わり。記録は塊をまたがない。
   ★塊を読む rom_to_ram はページ 1 なので第 4 バンクから呼べる(写す先 fz_buf はページ 3 = 5 面のオーバレイの変数)。 */
#include "game.h"
#include "vdp.h"
#include "rom.h"
#include "sys.h"
#include "assets.h"
#include "farz_data.h"

#define JIFFY (*(volatile u8 *)0xFC9E)
#define FZ_STEP   4                 /* 1 段のフレーム数(15Hz)。★書く時間がばらついても、書き始めはこの間隔にそろえる */
#define FZ_BUDGET (225u * 16u)      /* 書いてよいのはフレームの頭から 225 ライン(砂時計と同じ) */
enum { FZ_RUN, FZ_WAIT, FZ_DONE };

extern u16 g_frame_t0;

static u8 *fz_buf;                  /* 塊(256 バイト)の写し */
static u8  fz_pos, fz_k, fz_r14, fz_sy0, fz_j, fz_st, fz_ld;
static u16 fz_blk, fz_rx0, fz_budget, fz_f;         /* fz_f = 段(FZ_NF は 255 を超える) */

static void fz_load(void) {
    u16 off = (u16)(ASSET_FAR_OFF + ((fz_blk & 31) << 8));
    u8 bank = (u8)(ASSET_FAR_BANK + (fz_blk >> 5));
    if (off >= 8192) { off -= 8192; bank++; }
    rom_to_ram(bank, off, fz_buf, 256);
    fz_pos = 0; fz_ld = 1;
}

/* fz_buf[fz_pos] から記録を書く(asm)。戻り値 0 = 時間切れ、0xFE = 塊の終わり、0xFF = 段の終わり(fz_pos はその印を指す)。
   VRAM の番地 = リングの半分(x の 256 の位)<< 15 | 行 << 7 | バイトの位置(R#14 = 番地 >> 14)。リングの 256 / 512 の継ぎ目で 2 つに分ける。
   時刻を見る(と PCM の送出口を呼ぶ)のは 2 記録に 1 回(s6boss.c の hg_run と同じ作り) */
static u8 fz_run(void) __naked {
    __asm
        ld   a, (_fz_pos)
        ld   e, a
        ld   d, #0
        ld   hl, (_fz_buf)
        add  hl, de
        ld   a, #1
        ld   (_fz_k), a
        ld   a, #0xFF
        ld   (_fz_r14), a          ; R#14 はほかの書き込みが替えるので、呼ぶたびに覚え直す
    00010$:
        ld   a, (hl)
        cp   #0xFE
        jp   nc, 00090$            ; 塊・段の終わり
        ld   a, (_fz_k)
        dec  a
        ld   (_fz_k), a
        jr   nz, 00020$
        ld   a, #2
        ld   (_fz_k), a
        push hl
        ld   hl, (_g_vdp_wait_hook)
        ld   a, h
        or   l
        call nz, 00099$            ; PCM の送出口
        call _sys_timer            ; DE = 今
        ld   hl, (_g_frame_t0)
        ex   de, hl
        or   a
        sbc  hl, de
        ld   de, (_fz_budget)
        or   a
        sbc  hl, de
        pop  hl
        jr   c, 00020$
        xor  a                     ; 時間切れ
        jp   00090$
    00020$:
        ld   a, (_fz_sy0)
        add  a, (hl)
        ld   b, a                  ; B = リングの行
        inc  hl
        ld   a, (hl)               ; バイトの位置
        inc  hl
        add  a, a
        ld   e, a
        ld   a, #0
        adc  a, a
        ld   d, a                  ; DE = バイトの位置 * 2
        ld   c, (hl)               ; C = 長さ(最上位ビット = くり返し)
        inc  hl                    ; HL = 中身
        push hl
        ld   hl, (_fz_rx0)
        add  hl, de
        ld   a, h
        and  #1
        ld   d, a                  ; D = 半分(ページ 0 / 1)
        ld   a, l
        srl  a
        ld   e, a                  ; E = その行のバイト
        pop  hl
        bit  7, c
        jr   nz, 00040$
        ld   a, #128
        sub  e                     ; A = ページの継ぎ目までのバイト
        cp   c
        jr   c, 00030$
        call 00080$
        ld   b, c
        ld   c, #0x98
        otir
        jp   00010$
    00030$:                        ; 継ぎ目をまたぐ: 2 つに分ける
        push af
        call 00080$
        pop  af
        push bc
        ld   b, a
        ld   c, #0x98
        otir
        pop  bc
        neg
        add  a, c
        ld   c, a                  ; 残り
        call 00070$
        ld   b, c
        ld   c, #0x98
        otir
        jp   00010$
    00040$:                        ; くり返し: C = 個数、(HL) = 中身
        res  7, c
        ld   a, #128
        sub  e
        cp   c
        jr   c, 00045$
        call 00080$
        ld   b, c
        ld   a, (hl)
        inc  hl
    00041$:
        out  (0x98), a
        djnz 00041$
        jp   00010$
    00045$:                        ; 継ぎ目をまたぐ(A = 継ぎ目までの個数)
        push af
        call 00080$
        pop  af
        push bc
        ld   b, a
        ld   a, (hl)
    00046$:
        out  (0x98), a
        djnz 00046$
        pop  bc
        ld   a, #128
        sub  e
        neg
        add  a, c
        ld   c, a                  ; 残り
        call 00070$
        ld   b, c
        ld   a, (hl)
        inc  hl
    00047$:
        out  (0x98), a
        djnz 00047$
        jp   00010$
    00070$:                        ; 次の半分の行の頭へ(D を替え、E = 0)
        ld   a, d
        xor  #1
        ld   d, a
        ld   e, #0
    00080$:                        ; VRAM の番地(D = 半分、B = 行、E = バイト)。A だけ壊す
        ld   a, d
        add  a, a
        bit  7, b
        jr   z, 00081$
        inc  a
    00081$:
        push hl
        ld   hl, #_fz_r14
        di
        cp   (hl)
        jr   z, 00082$             ; 同じなら R#14 を書かない
        ld   (hl), a
        out  (0x99), a
        ld   a, #0x8E
        out  (0x99), a
    00082$:
        pop  hl
        ld   a, b
        rrca
        and  #0x80
        or   e
        out  (0x99), a
        ld   a, b
        srl  a
        and  #0x3F
        or   #0x40
        out  (0x99), a
        ei
        ret
    00090$:
        ld   b, a
        ld   de, (_fz_buf)
        or   a
        sbc  hl, de
        ld   a, l
        ld   (_fz_pos), a
        ld   a, b
        ret
    00099$:
        jp   (hl)
    __endasm;
}

/* 今の段を書く(フレームの頭から fz_budget まで)。1 = 書き終えた */
static u8 fz_write(void) {
    u16 t = sys_timer();                                      /* ★使い切る書き込みの時間は「重かった」に数えない(g_elastic) */
    u8 r;
    for (;;) {
        if (!fz_ld) fz_load();
        r = fz_run();
        if (r == 0xFF) {
            if (++fz_pos == 0) { fz_blk++; fz_ld = 0; }       /* 印が塊の最後のバイトだった */
            r = 1; break;
        }
        if (r != 0xFE) { r = 0; break; }
        fz_blk++; fz_ld = 0;
    }
    g_elastic += (u16)(sys_timer() - t);
    return r;
}

/* 始める。buf = 塊の写しの場所(ページ 3、256 バイト)。世界は止めてあり、画面の x = FZ_XB が 8 の倍数のリングの位置(s5.c) */
void fz_start(u8 *buf) {
    fz_buf = buf; fz_blk = 0; fz_ld = 0; fz_f = 0; fz_st = FZ_RUN; fz_j = JIFFY;
    fz_budget = FZ_BUDGET;
    fz_rx0 = (u16)((g_wx + FZ_XB) & 510u);
    fz_sy0 = g_vscroll;                                       /* 接近のカメラは s = 0(画面の行 = s) */
}

/* 毎フレーム(VRAM の窓)。戻り値: 下位 = 今の段の d(艦首を FZ_XB から左へずらすドット)、0x8000 = 最後の段まで見せ終えた */
u16 fz_vram(void) {
    if (fz_st == FZ_DONE) return 0x8000u;
    if (fz_st == FZ_WAIT) {
        if ((u8)(JIFFY - fz_j) < FZ_STEP) return fz_dtab[fz_f];
        fz_j = JIFFY;                                         /* ★遅れたらそこから数え直す(取り戻すと間隔が揺れる。砂時計) */
        if (++fz_f >= FZ_NF) { fz_st = FZ_DONE; return 0x8000u; }
        fz_st = FZ_RUN;
    }
    if (fz_write()) fz_st = FZ_WAIT;
    return fz_dtab[fz_f];
}
