/* raster.c — ラスターリスト(raster.h 参照)。
   出典: ZeroNoHoukouR/src/core/raster.c(H.KEYI フック・S#1 FH 判定・VBLANK での仕切り直し)を、
   ダブルバッファの表・HR ポーリング帯・直接割込み(ページ 0 RAM)に拡張。
   ★asm から参照する変数・関数は非 static(static だと参照が消える/名前が変わる)。 */
#include "raster.h"
#include "vdp.h"
#include "frame.h"

RasEntry ras_buf0[RAS_MAX];
RasEntry ras_buf1[RAS_MAX];
RasEntry *volatile ras_front = ras_buf0;   /* ISR が読む表 */
RasEntry *volatile ras_backp = ras_buf1;   /* メインが組む表 */
volatile u8 ras_front_n;
volatile u8 ras_back_n;
volatile u8 ras_swap;          /* 1 = 次の VBLANK で表を差し替える */
RasEntry *volatile ras_ptr;    /* 次に適用する項目 */
volatile u8 ras_left;          /* 残り項目数 */
volatile u8 ras_armed;         /* 今 R#19 に仕込んである値 */
volatile u8 ras_r19;           /* ラスターの次の項目の R#19(ras_left > 0 のとき有効) */
static u8 ras_r1v;              /* RAS_BLANK: 戻す R#1 */
volatile u8 ras_vs;            /* 今の R#23(RAS_REGS で R#23 を書くと更新)。R#19 はこれを足した値で仕込む */
#ifndef RAS_GAME                /* ★本体(RAS_GAME)は方式 B だけなので、刻み方式(方式 A/C)と BIOS 方式は入れない(常駐の空き。M7 の後) */
volatile u8 ras_tl;            /* 刻み方式: 仕込んだ刻みの表示ライン(0, 2, .., 244)。R#19 = ras_tl + ras_vs。
                                  ★刻みを R#19 の値で数えると、帯で R#23 が変わったところで鎖が切れる(M7) */
volatile u8 ras_lkey;          /* 次の項目の表示ラインのキー(line - g_ras_bias。R#23 を足さない)。刻み方式で比べる */
#endif
volatile u16 g_ras_irqs;       /* 割込みの総数(計測用) */
volatile u16 g_ras_fh;         /* うち FH(走査線)の数 */
volatile u16 g_ras_vb;         /* うち VBLANK の数 */
volatile u16 g_ras_rearm;      /* 刻みの鎖が途切れて張り直した回数(計測用。0 が正常) */
volatile u8 g_ras_mark;        /* RAS_MARK の項目が適用されるたびに +1 */
#ifndef RAS_GAME
volatile u8 ras_kick;          /* 1 = 次の VBLANK で刻みを張り直す(ras_kick_ticks) */

/* PCM(走査線割込みに相乗り。指示書 8.6 の方式 A/C)。pcm_play() で開始する。 */
volatile u8  pcm_on;           /* 1 = 刻み方式(2 ラインごとの割込みでサンプルを出す) */
volatile u8  pcm_fill;         /* 1 = 割込みの穴(246..261 行、HR ポーリング帯)も埋める(方式 C) */
const u8 *volatile pcm_ptr;
volatile u16 pcm_left;
volatile u8  pcm_pend;         /* 1 = 次の VBLANK で pcm_on にする(フレームの途中から始めない) */
const u8 *volatile pcm_next_ptr;   /* 鳴り終わったら続けて鳴らすデータ(pcm_queue)。切れ目を作らない */
volatile u16 pcm_next_left;
#endif
void (*volatile ras_pcm_hook)(void);   /* 0 以外: 割込みの入口と HR ポーリング帯の 1 ラインごとに呼ぶ(PCM 方式 B の送出口)。
                                          A とフラグ以外を壊さないこと */
#ifndef RAS_GAME
volatile u8  ras_pstep = 2;     /* 刻みの幅(ライン)。2 = 7.9kHz(M0)、4 = 3.9kHz(本体。CPU の消費が半分。M7) */
volatile u8  ras_pmask = 1;     /* ras_pstep - 1 */
volatile u8  ras_regs_hit;      /* 刻み方式: この刻みでレジスタ列(RAS_REGS)の項目を適用した */
volatile u8  pcm_end;          /* 1 = 刻み 212 で刻み方式をやめる(pcm_stop) */
#endif

volatile u8 g_ras_bias = 4;    /* openMSX(FS-A1ST)で境界のずれが 0 になる値。実機で取り直す */
volatile u8 g_ras_busy_border;

static u8 isr_mode;
static u8 saved_a8, saved_sub, sub_used;

/* ---- 割込みハンドラ ----
   ★2 つの動き方がある:
     項目方式(PCM なし): 次の R#19 = ラスターの次の項目の行。
     刻み方式(PCM あり): 2 ラインごとの固定の刻み(R#19 = 212, 214, .., 244, 0, 2, .., 210)。
       割込みに入ったら**何よりも先に次の刻みを仕込み**、それからサンプルを出し、その刻みまでに来ている
       ラスター項目を適用する(項目は最大 1 ライン早まる)。後の処理が 1.5 ライン近くかかっても次を取りこぼさない。
       ★サンプルとラスターを別々の事象として「近い方を仕込む」作りは、両方が重なった割込みの処理で
         次の仕込みが遅れ、1 フレーム丸ごと止まった(M0-3 で 2 秒に 30 回強)。
   ★R#19 が 246..255 の割込みは起きない(探り ROM で実測)。刻み 244 の次は 0 にし、方式 C ではその間
     (246..260 の 8 サンプル)を 244 の割込みの中で HR を数えて出す。
   ★turboR の R800 は VDP への I/O 1 回に 8.6us かかる(1 ラインに 7 回ほど。探り ROM で実測)。
     直接方式では R#15 を常に 1 にしておき、FH の判定を IN 1 回で済ませる。
   ★レジスタの約束: ISR 本体は C に S#0(bit7 = VBLANK)を持つ。下請けは C を壊さない。 */
void ras_isr_direct(void) __naked {
    __asm
        push af
        push bc
        push de
        push hl
        push ix
        ld   a, (_g_ras_busy_border)
        or   a
        jr   z, 00001$
        out  (0x99), a
        ld   a, #0x87              ; R#7 = 枠線色(ISR 中)
        out  (0x99), a
    00001$:
        ld   hl, (_g_ras_irqs)
        inc  hl
        ld   (_g_ras_irqs), hl
        in   a, (0x99)             ; S#1(R#15 は常に 1。読むと FH が落ちる)
        rra
        jr   nc, 00002$
        ld   c, #0
        call ras_on_fh
        jr   00003$                ; 同時に VBLANK が来ていれば、戻った直後にもう一度割り込む
    00002$:
        xor  a                     ; FH でなければ VBLANK: S#0 を読んでフラグを落とす
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 0
        out  (0x99), a
        in   a, (0x99)             ; S#0
        ld   c, a
        ld   a, #1
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 1(待機値へ)
        out  (0x99), a
        bit  7, c
        jr   z, 00003$
        ld   hl, (0xFC9E)          ; BIOS の代わりに JIFFY を進める
        inc  hl
        ld   (0xFC9E), hl
        call ras_on_vb
    00003$:
        ld   a, (_g_ras_busy_border)
        or   a
        jr   z, 00004$
        xor  a
        out  (0x99), a
        ld   a, #0x87              ; R#7 = 0
        out  (0x99), a
    00004$:
        call ras_hook              ; PCM 方式 B の送出口。★レジスタを書く仕事を全部終えてから(入口に置くと画面モードの
                                   ;   切り替えが数 us 遅れ、M0-4 の分割の境界がずれた)
        pop  ix
        pop  hl
        pop  de
        pop  bc
        pop  af
        ei
        ret
    __endasm;
}

/* BIOS 方式: BIOS の割込みハンドラ → H.KEYI から呼ばれる。R#15 の待機値は 0(BIOS が S#0 を読むため)。
   FH でなければ VBLANK とみなし、ret で BIOS へ戻す(BIOS が S#0 を読んで H.TIMI を呼ぶ)。PCM は使わない。 */
#ifndef RAS_GAME
void ras_isr_bios(void) __naked {
    __asm
        push bc
        push de
        push hl
        push ix
        ld   hl, (_g_ras_irqs)
        inc  hl
        ld   (_g_ras_irqs), hl
        ld   a, (_g_ras_busy_border)
        or   a
        jr   z, 00001$
        out  (0x99), a
        ld   a, #0x87
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
        ld   c, #0
        bit  0, b
        jr   z, 00002$
        call ras_on_fh
        jr   00003$
    00002$:
        call ras_on_vb             ; FH でなければ VBLANK とみなす
    00003$:
        ld   a, (_g_ras_busy_border)
        or   a
        jr   z, 00004$
        xor  a
        out  (0x99), a
        ld   a, #0x87
        out  (0x99), a
    00004$:
        pop  ix
        pop  hl
        pop  de
        pop  bc
        ret
#else
void ras_isr_subs(void) __naked {    /* 直接方式の下請けだけ(本体は BIOS 方式を使わない) */
    __asm
#endif

        ; ---- FH ----
    ras_on_fh:
        ld   hl, (_g_ras_fh)
        inc  hl
        ld   (_g_ras_fh), hl
        ld   a, (_ras_armed)
        ld   d, a                  ; D = 今の割込みライン(R#19 値)
#ifndef RAS_GAME
        ld   a, (_pcm_on)
        or   a
        jp   z, 00020$
        ; -- 刻み方式: 1) 次の刻みを先に仕込む(D = この刻みの表示ライン) --
        ld   a, (_ras_tl)
        ld   d, a
        ld   e, a
        ld   a, (_ras_pstep)
        add  a, e
        cp   #246
        jr   c, 00011$
        xor  a                     ; 244 の次は 0(246..255 の割込みは起きない)
    00011$:
        call ras_arm_tl
        ; -- 2) サンプル --
        call pcm_out
        ld   a, d
        cp   #212
        jr   nz, 00012$
        ; -- 刻み 212 = VBLANK の仕事(刻み方式では VBLANK 割込みを止めている) --
        ld   hl, (0xFC9E)          ; JIFFY
        inc  hl
        ld   (0xFC9E), hl
        ld   hl, (_g_ras_vb)
        inc  hl
        ld   (_g_ras_vb), hl
        push de
        call _ras_vblank           ; 表の差し替え、先頭(line 0)の適用
        pop  de
        ; -- 刻み方式の終わり(pcm_stop): ★ここ(VBLANK の仕事を済ませた所)でやめる。本体からすぐ VBLANK 割込みを
        ;   戻すと、このフレームの JIFFY を刻み 212 と VBLANK で 2 回数え、遅れたフレームになった(M7) --
        ld   a, (_pcm_end)
        or   a
        jr   z, 00012$
        xor  a
        ld   (_pcm_end), a
        ld   (_pcm_on), a
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 0
        out  (0x99), a
        in   a, (0x99)             ; S#0: このフレームの VBLANK のフラグを落とす
        ld   a, (_g_vdp_status_rest)
        out  (0x99), a
        ld   a, #0x8F
        out  (0x99), a
        ld   a, (0xF3E0)           ; RG1SAV(IE0 = 1)
        out  (0x99), a
        ld   a, #0x81
        out  (0x99), a
        jp   ras_arm_item          ; 次のフレームの最初の項目
    00012$:
        ; -- 3) この刻み(と次のライン)までに来ているラスター項目 --
        ld   a, (_ras_left)
        or   a
        jr   z, 00015$
        ld   a, (_ras_lkey)
        sub  #212
        ld   e, a                  ; E = 項目のキー(VBLANK からの順番、表示ライン)
        ld   a, d
        sub  #212
        inc  a                     ; 今の刻みのキー + 1
        cp   e
        jr   c, 00015$             ; まだ先
        push de
        call _ras_apply_next
        pop  de
        jr   00012$
        ; -- 4) 刻み 244: 方式 C の穴埋め(246..260 の 8 サンプル。0 は仕込み済み)。
        ;   ★ラスター項目の適用より後に置く。先に穴埋め(16 ライン)をすると、その後の項目(画面モードの
        ;     切り替えで約 2 ライン)で割込みを抜けるのが次のフレームの 2 ライン目近くになり、刻み 0 の処理が
        ;     遅れて刻み 2 を仕込み損ねた(M0-4)。
    00015$:
        ; -- 項目が R#23 を変えたら、刻みを新しい R#23 で仕込み直す --
        ;   ★割込みは表示ライン (R#19 - R#23) ちょうどで来る。項目を適用し終えた頃には 2 ライン先の刻みが
        ;     過ぎていることがあり、取りこぼすと 1 フレーム近く止まった(M7、起動ロゴの 130 行で R#23 が 58 → 8)。
        ;     1 つ先(4 ライン先)を仕込み、飛ばしたサンプルはここで出す。
        ;   レジスタ列の項目(画面モードの切り替えなど、2 ライン近くかかる)の後も同じ(M7、出撃の帯の終わりで
        ;   毎フレーム 1 つ取りこぼし、JIFFY が 2 フレームに 1 回しか進まず、声の間は曲もゲームも半分の速さになった)
        ld   a, (_ras_regs_hit)
        or   a
        jr   z, 00018$
        xor  a
        ld   (_ras_regs_hit), a
        jr   00019$
    00018$:
        ld   a, (_ras_vs)
        ld   e, a
        ld   a, (_ras_tl)
        add  a, e
        ld   e, a
        ld   a, (_ras_armed)
        cp   e
        jr   z, 00016$
    00019$:
        ld   a, (_ras_tl)
        cp   #212
        jr   z, 00017$             ; 刻み 212(VBLANK の仕事)は飛ばさない
        ld   e, a
        ld   a, (_ras_pstep)
        add  a, e
        cp   #246
        jr   c, 00017$
        xor  a
    00017$:
        call ras_arm_tl
        ld   a, (_pcm_fill)
        or   a
        call nz, pcm_out
    00016$:
        ld   a, d
        cp   #244
        ret  nz
        ld   a, (_pcm_fill)
        or   a
        ret  z
        jp   pcm_gap
#endif
        ; -- 項目方式 --
        ;   ★次の項目が 6 ライン未満なら、割込みを待たずに続けて適用する(項目は少し早まる)。割込みは
        ;     R#19 の 3 ライン後あたりで来て、適用に 2〜3 ラインかかるので、近い項目に R#19 を仕込むと
        ;     行が過ぎていて、そのフレームの残りが全部消える(M2: 降下で帯の境目が動くと項目どうしが近づく)。
        ;     リストを組む側(stage.c)も項目を RAS_GAP ライン以上あける。
    00020$:
        ld   a, (_ras_left)
        or   a
        ret  z
        ld   a, (_ras_r19)
        cp   d
        jp   nz, ras_arm_item
    00021$:
        push de
        call _ras_apply_next
        pop  de
        ld   a, (_ras_left)
        or   a
        ret  z
        ld   a, (_ras_r19)
        sub  d
        cp   #6
        jr   c, 00021$
        jp   ras_arm_item

        ; ---- VBLANK ----
    ras_on_vb:
#ifndef RAS_GAME
        ; ★刻み方式の最中に VBLANK 割込みが来た = 本体が R#1 を書き直して IE0 が戻った(表示の on/off など)。
        ;   刻み 212 が VBLANK の仕事をするので、ここでは IE0 を止め直すだけ(JIFFY の二重の +1 も戻す)。
        ;   放っておくと項目方式の R#19 が仕込まれ、刻みが 1 フレームに 1 つになった(M7、起動ロゴの声)。
        ld   a, (_pcm_on)
        or   a
        jr   z, 00030$
        ld   a, (_ras_kick)
        or   a
        jr   nz, 00030$            ; 見張りが張り直しを頼んだ VBLANK は通す
        ld   hl, (0xFC9E)
        dec  hl
        ld   (0xFC9E), hl
        ld   a, (0xF3E0)           ; RG1SAV
        and  #0xDF                 ; IE0 = 0
        out  (0x99), a
        ld   a, #0x81
        out  (0x99), a
        ret
    00030$:
#endif
        ld   hl, (_g_ras_vb)
        inc  hl
        ld   (_g_ras_vb), hl
        call _ras_vblank           ; 表の差し替え、先頭(line 0)の適用
#ifdef RAS_GAME
        jp   ras_arm_item
#else
        ld   a, (_pcm_pend)
        ld   e, a
        ld   a, (_ras_kick)
        or   e
        jp   z, ras_arm_item       ; 項目方式
        ; -- 刻み方式を始める(または張り直す): VBLANK 割込みを止め、最初の刻みは 212 --
        ;   ★刻み方式では VBLANK の仕事を刻み 212 の中で行う。VBLANK 割込みと刻みの前後は条件しだいで
        ;     入れ替わり(陽炎帯の有無で変わった)、VBLANK の処理(1.4 ライン)で次の刻みを取りこぼした(M0-3)。
        xor  a
        ld   (_pcm_pend), a
        ld   (_ras_kick), a
        inc  a
        ld   (_pcm_on), a
        ld   a, (0xF3E0)           ; RG1SAV
        and  #0xDF                 ; IE0 = 0
        out  (0x99), a
        ld   a, #0x81              ; R#1
        out  (0x99), a
        ld   a, #212
        jp   ras_arm_tl

        ; ---- 刻み方式の仕込み: 刻みの表示ライン A → R#19 = A + R#23(E を使う) ----
    ras_arm_tl:
        ld   (_ras_tl), a
        ld   e, a
        ld   a, (_ras_vs)
        add  a, e
        jp   ras_arm
#endif

        ; ---- 項目方式の仕込み: 次の項目の行 ----
    ras_arm_item:
        ld   a, (_ras_left)
        or   a
        ret  z
        ld   a, (_ras_r19)
        ; そのまま ras_arm へ
        ; ---- R#19 = A ----
    ras_arm:
        ld   (_ras_armed), a
        out  (0x99), a
        ld   a, #0x93              ; R#19
        out  (0x99), a
        ret

#ifndef RAS_GAME
        ; ---- 方式 C の穴埋め: HR を 16 回数え、2 本目ごとに出す(246, 248, .., 260) ----
    pcm_gap:
        push bc
        ld   a, #2
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 2(HR を見る)
        out  (0x99), a
        ld   b, #16
    00040$:
        in   a, (0x99)
        and  #0x20
        jr   nz, 00040$
    00041$:
        in   a, (0x99)
        and  #0x20
        jr   z, 00041$
        ld   a, (_ras_pmask)       ; 刻みの幅ごとに 1 つ(2 なら 8 個、4 なら 4 個)
        and  b
        dec  a
        call z, pcm_out
        djnz 00040$
        ld   a, (_g_vdp_status_rest)
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 待機値
        out  (0x99), a
        pop  bc
        ret

        ; ---- 1 サンプル出力(A, H, L を使う) ----
    pcm_out:
        ld   hl, (_pcm_left)
        ld   a, h
        or   l
        jr   nz, 00050$
        ld   hl, (_pcm_next_left)  ; 鳴り終わった: 予約があれば続ける
        ld   a, h
        or   l
        ret  z
        ld   (_pcm_left), hl
        ld   hl, (_pcm_next_ptr)
        ld   (_pcm_ptr), hl
        ld   hl, #0
        ld   (_pcm_next_left), hl
        ld   hl, (_pcm_left)
    00050$:
        dec  hl
        ld   (_pcm_left), hl
        ld   hl, (_pcm_ptr)
        ld   a, (hl)
        inc  hl
        ld   (_pcm_ptr), hl
        out  (0xA4), a
        ret
#endif
    __endasm;
}

/* 項目 1 つを適用し、次の項目の R#19(ras_r19)を求める。★C(呼び出し元の S#0)は壊さない。 */
void ras_apply_next(void) __naked {
    __asm
        ld   ix, (_ras_ptr)
        ld   d, 1 (ix)             ; flags
        bit  5, d
        jr   z, 00014$
        ld   hl, #_g_ras_mark      ; RAS_MARK: 適用されたことをメインへ知らせる
        inc  (hl)
    00014$:
        bit  6, d
        jp   nz, 00030$            ; RAS_BLANK
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
        bit  4, d
        jr   z, 00009$
        ; ---- レジスタ列(RAS_REGS): (番号, 値) × haze_n。画面モードの切り替え(M0-4)など ----
#ifndef RAS_GAME
        ld   a, #1
        ld   (_ras_regs_hit), a    ; 刻み方式: 書き終わる頃には次の刻みが過ぎていることがある(on_fh が 1 つ先を仕込む)
#endif
        ld   e, 7 (ix)
        ld   l, 8 (ix)
        ld   h, 9 (ix)
    00008$:
        call 00050$
        dec  e
        jr   nz, 00008$
        ; ★仕込み済みの R#19 を書き直す。刻み方式は先に次の刻みを仕込んでから項目を適用するが、
        ;   画面モードを書き換えると仕込んだ割込みが失われ、刻みの鎖が 1 フレームおきに切れた
        ;   (M0-4、openMSX。モードの変更で走査線割込みの予定が立て直されるためと思われる)。
        ld   a, (_ras_armed)
        out  (0x99), a
        ld   a, #0x93              ; R#19
        out  (0x99), a
        jp   00005$
    00009$:
        bit  2, d
        jp   z, 00005$
        jp   00040$
        ; ---- RAS_BLANK(raster.h): 帰線で表示を消す → 横スクロール・レジスタ列 → 次の帰線で戻す ----
        ;   帰線では 0x9B への OUT 1 回だけ(R#17 = 1、自動加算なし。R#15 = 2 で HR を見る)。パレットは消す前に書く
    00030$:
        push bc                    ; ★C は呼び出し元の S#0
        bit  1, d
        jr   z, 00031$
        ld   a, 4 (ix)
        out  (0x99), a
        ld   a, #0x90              ; R#16 = パレット番号
        out  (0x99), a
        ld   a, 5 (ix)
        out  (0x9A), a
        ld   a, 6 (ix)
        out  (0x9A), a
    00031$:
        ld   e, 7 (ix)
        ld   l, 8 (ix)
        ld   h, 9 (ix)
        push hl
        ld   a, e
        add  a, a
        dec  a                     ; 最後の組の値 = 2n - 1
        add  a, l
        ld   l, a
        jr   nc, 00032$
        inc  h
    00032$:
        ld   a, (hl)
        ld   (_ras_r1v), a         ; 戻す R#1
        and  #0xBF                 ; BL = 0
        ld   b, a
        pop  hl
        ; 3 組目から最後の 1 つ前(HUD では属性表の R#5・R#11)は消す前に書く(FH を待つ前に。★FH は行の終わりに立つので、そのあとに書くと
        ;   その行の帰線を逃した。消す行を 2 行に)。
        ;   ★先頭の 2 組(ページ R#2・縦スクロール R#23)と最後の R#1 だけを消している間に書く
        push hl
        inc  hl
        inc  hl
        inc  hl
        inc  hl
        bit  3, d
        jr   nz, 00048$            ; RAS_BLANK_ALL: 全部の組を消している間に書く(画面モードの切り替え)
        ld   a, e
        sub  #3
        jr   z, 00048$
        jr   c, 00048$
        push bc
        ld   c, a
    00049$:
        call 00050$
        dec  c
        jr   nz, 00049$
        pop  bc
        ld   e, #3                 ; 消している間に書くのは 2 組 + 最後の R#1
    00048$:
        pop  hl
        ld   a, #0x81
        out  (0x99), a
        ld   a, #0x91              ; R#17 = 1(自動加算なし)。★これも FH を待つ前に
        out  (0x99), a
        ld   c, #0x9B
        ld   a, #2
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 2(S#2: HR)
        out  (0x99), a
        ; ★消すのは入ってから最初の帰線(割込みの遅れは 1 ライン未満の約束。FH で 2 行先を待つ形は、待ちが 1 フレームに
        ;   4〜5 行になり、1 周の遅れが増えた)
        call 00038$                ; その行の帰線まで待つ
        out  (c), b                ; R#1 = 表示を消す
        bit  0, d
        jr   z, 00033$
        ld   a, 2 (ix)
        out  (0x99), a
        ld   a, #0x9A              ; R#26
        out  (0x99), a
        ld   a, 3 (ix)
        out  (0x99), a
        ld   a, #0x9B              ; R#27
        out  (0x99), a
    00033$:
        ; ★表示を消している間は FH が立たないので、戻す行は数えて決める: 1 行に 2 組ずつ書いて帰線を待つ
        ;   (書き込みの長さが帰線の境目に重なると、戻す行が 1 行ずれて HUD の上の行が出たり出なかったりした)
        dec  e                     ; 最後の R#1 は戻すときに
    00034$:
        call 00038$                ; 次の行の始まり(帰線)
        ld   a, e
        or   a
        jr   z, 00036$
        ld   d, #2
    00035$:
        call 00050$
        dec  e
        jr   z, 00034$
        dec  d
        jr   nz, 00035$
        jr   00034$
    00036$:
        ld   a, (_ras_r1v)
        out  (c), a                ; R#1 = 表示を戻す(帰線の中。消してから 3 行)
        ld   a, (_g_vdp_status_rest)
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 待機値
        out  (0x99), a
        ld   a, (_ras_armed)
        out  (0x99), a
        ld   a, #0x93              ; R#19 を書き直す(RAS_REGS と同じ)
        out  (0x99), a
        pop  bc
        jp   00005$
        ; (HL) の (番号, 値) を 1 組書いて HL を進める。R#23 は覚える(以降の R#19 に足す)。A を壊す
    00050$:
        inc  hl
        ld   a, (hl)               ; 値
        out  (0x99), a
        dec  hl
        ld   a, (hl)               ; 番号
        inc  hl
        inc  hl
        cp   #23
        jr   nz, 00051$
        dec  hl
        ld   a, (hl)
        inc  hl
        ld   (_ras_vs), a
        ld   a, #23
    00051$:
        or   #0x80
        out  (0x99), a
        ret
        ; 表示期間に入るのを待ってから帰線(HR = 1)を待つ。A を壊す
    00038$:
        in   a, (0x99)
        and  #0x20
        jr   nz, 00038$
    00039$:
        in   a, (0x99)
        and  #0x20
        jr   z, 00039$
        ret
    00040$:
        ; ---- HR ポーリング帯 ----
        ; 帰線期間(HR=1)に入ってからの書き込みを最小にするため、R#17(間接レジスタ番号)は
        ; 表示期間のうちに設定し、帰線では 0x9B への OUT だけを行う。
        ;   RAS_HAZE_FINE: R#27 だけを毎ライン(OUT 1 回。R#17 = 27 | 0x80 で自動加算なし)
        ;   それ以外      : R#26, R#27 を毎ライン(OUT 2 回。R#17 = 26 を毎ライン設定し直す)
        ; 刻み方式(pcm_on)ではこの帯の間も 2 ラインごとにサンプルを受け持つ(D = 次のサンプルまでの HR 数)。
        ; pcm_fill なら出力、そうでなければ数えるだけ(方式 A の欠落)。帯が終わったら次の刻みを仕込み直す。
        push bc                    ; ★C は呼び出し元の S#0(bit7 = VBLANK)。outi 用に C を使うので退避する
                                   ;   (退避し忘れて C=0x9B のまま戻り、陽炎帯の終わりを毎フレーム VBLANK と誤判定していた)
#ifndef RAS_GAME
        ld   d, #0
        ld   a, (_pcm_on)
        or   a
        jr   z, 00020$
        ld   a, (_ras_pstep)       ; 今の刻みのサンプルは出し済み。次は刻みの幅だけ後の HR
        ld   d, a
    00020$:
#endif
        ld   e, 7 (ix)             ; ライン数
        ld   l, 8 (ix)
        ld   h, 9 (ix)             ; (r26, r27) 表
        ld   c, #0x9B
        ld   a, #2
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 2(S#2: HR を見る)
        out  (0x99), a
        bit  3, 1 (ix)
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
#ifdef RAS_GAME
        call ras_hook
#else
        call hz_pcm
#endif
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
#ifdef RAS_GAME
        call ras_hook
#else
        call hz_pcm
#endif
        dec  e
        jr   nz, 00010$
    00012$:
        ld   a, (_g_vdp_status_rest)
        out  (0x99), a
        ld   a, #0x8F              ; R#15 = 待機値
        out  (0x99), a
#ifndef RAS_GAME
        ld   a, (_pcm_on)
        or   a
        jr   z, 00013$
        ; 刻み方式: 帯の後の次の刻み = (帯を始めた刻み + ライン数 + 4) の偶数。
        ;   ★R#19 = r の割込みは表示ライン r + 3 あたりで来る(補正 g_ras_bias = 4 と同じずれ)ので、帯は
        ;     刻みより 3 ライン後ろで終わる。+2 だと過ぎた刻みを仕込み、割込みが連発して VBLANK の処理が
        ;     後回しになり、刻みの鎖が切れた(M0-3)。1 つ先を仕込み、飛ばす 1 個は方式 C ならここで出す。
        ld   a, (_pcm_fill)
        or   a
        call nz, pcm_out
        ld   a, (_ras_pstep)
        ld   b, a
        ld   a, (_ras_tl)
        sub  b                     ; 帯を始めた刻み(入口で 1 つ先を仕込み済み)
        add  a, 7 (ix)
        add  a, #4
        add  a, b
        dec  a
        ld   c, a                  ; 刻みの幅の倍数へ切り上げ
        ld   a, (_ras_pmask)
        cpl
        and  c
        call ras_arm_tick
#endif
    00013$:
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
        ld   b, a
#ifndef RAS_GAME
        ld   (_ras_lkey), a
#endif
        ld   a, (_ras_vs)
        add  a, b                  ; ★R#19 は「表示の行 + R#23」で比べられる(縦スクロール中は足す)
        ld   (_ras_r19), a
        ret

#ifndef RAS_GAME
        ; R#19 = A(刻み方式で帯の後に張り直す)
    ras_arm_tick:
        ld   (_ras_tl), a          ; 表示ライン → R#19 = + R#23
        ld   b, a
        ld   a, (_ras_vs)
        add  a, b
        ld   (_ras_armed), a
        out  (0x99), a
        ld   a, #0x93
        out  (0x99), a
        ret

#endif
        ; 送出口のフック(ras_pcm_hook)。HL を守り、A とフラグだけ壊す
    ras_hook:
        ld   a, (_ras_pcm_hook + 1)   ; ★空なら 3 命令で帰る(HR ポーリング帯で 1 ラインごとに呼ぶので軽く。関数の番地の
        or   a                        ;   上位は 0 にならない)
        ret  z
        push hl
        ld   hl, (_ras_pcm_hook)
        call ras_call_hl           ; ★___sdcc_call_hl(ライブラリ)はページ 2 にあり、ROM を読む間(ページ 2 が別の
                                   ;   バンク)の割込みで暴走した(M7)。ページ 1 の自前のものを使う
    00090$:
        pop  hl
        ret
    ras_call_hl:
        jp   (hl)

#ifndef RAS_GAME
        ; HR ポーリング帯の 1 ラインごとの PCM(D = 次のサンプルまでのライン数。0 = PCM なし)
    hz_pcm:
        ld   a, d
        or   a
        jp   z, ras_hook           ; 刻み方式でなければ、方式 B の送出口(1 ラインごと)
        dec  d
        ret  nz
        ld   a, (_ras_pstep)
        ld   d, a
        ld   a, (_pcm_fill)
        or   a
        ret  z
        push hl
        call pcm_out
        pop  hl
        ret
#endif
    __endasm;
}

/* VBLANK: 表の差し替え → 先頭(line==0)の適用 → 最初の項目の R#19(ras_r19)。
   R#19 の仕込みは呼び出し元(ras_on_vb)が行う。 */
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
        jp   z, _ras_apply_next    ; line==0 は今ここで適用(次の項目の ras_r19 も求まる)
        ld   b, a
        ld   a, (_g_ras_bias)
        ld   d, a
        ld   a, b
        sub  d
        ld   b, a
#ifndef RAS_GAME
        ld   (_ras_lkey), a
#endif
        ld   a, (_ras_vs)
        add  a, b
        ld   (_ras_r19), a
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
    volatile u8 *rg0 = (volatile u8 *)0xF3DF;   /* RG0SAV */
#ifdef RAS_GAME
    ras_front_n = ras_back_n = ras_left = 0;
    isr_mode = RAS_ISR_BIOS;
    ras_set_isr(RAS_ISR_DIRECT);                /* ★本体は直接方式だけ。入れてから走査線割込みを有効に(先に有効に
                                                   すると BIOS の割込みが FH を落とさず、割込みが続いて止まる) */
    __asm di __endasm;
#else
    volatile u8 *h = (volatile u8 *)0xFD9A;     /* H.KEYI */
    ras_front_n = ras_back_n = ras_left = 0;
    isr_mode = RAS_ISR_BIOS;
    __asm di __endasm;
    h[1] = (u8)(u16)ras_isr_bios;
    h[2] = (u8)((u16)ras_isr_bios >> 8);
    h[0] = 0xC3;
#endif
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
    g_vdp_status_rest = (mode == RAS_ISR_DIRECT) ? 1 : 0;   /* 直接方式は S#1 を常時選んでおく */
    VDP_CTRL = g_vdp_status_rest;
    VDP_CTRL = 0x8F;
    __asm ei __endasm;
    return isr_mode;
}

