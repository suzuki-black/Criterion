/* snd.c — 音のドライバ(snd.h 参照)。★このファイルはページ 1(0x8000 未満)に置く(Makefile が検査)。
   曲を読む間(seq_run)はページ 2 が ROM の曲のバンクになるので、ここから呼ぶのはこのファイルの関数だけ。
   ライブラリの呼び出し(掛け算・割り算)も使わない(ライブラリはページ 2 にある)。

   曲データ(tools/gen_sound.py。番地は ROM の窓 0x8000..0xBFFF の中):
     頭: [0..1] テンポ(1 フレームの刻み数 × 256。24 刻み = 4 分音符) [2] 続けて鳴らす曲(0xFF = 無し)
         [3..10] OPLL のユーザー音色 [11..30] チャンネル 10 本の番地(0 = 使わない)
     チャンネルの命令:
       0x00..0x5F 音(o0c からの半音。リズムのチャンネルでは打楽器のビット BD 16 SD 8 TOM 4 CYM 2 HH 1)+ 長さ
       0x60 休み + 長さ / 0x61 延長 + 長さ(音を切らずに待つ)
       0x80..0x8F 音量 / 0x90..0x9F 音色(FM)・エンベロープ(PSG)
       0xA0 q(音を切るまでの刻みを長さから引く) / 0xA1 チャンネルのデチューン(s8) / 0xA2 次の音をつなぐ(レガート)
       0xA3 PCM / 0xA4 小節の頭(CUE) / 0xA5 PSG のノイズ(0 = 音、1 = ノイズ) / 0xA6 テンポ(u16)
       0xFE 飛ぶ(u16 番地) / 0xFF 終わり */
#include "snd.h"
#include "rom.h"
#include "raster.h"
#define SND_TABLES
#include "snd_data.h"

__sfr __at(0x7C) OPLL_A;
__sfr __at(0x7D) OPLL_D;
__sfr __at(0xA0) PSG_A;
__sfr __at(0xA1) PSG_W;
__sfr __at(0xA5) PCM_CTRL;

extern u8 ramx_a8;                   /* sys.c: カートリッジのスロット(ページ 1-2) */

u8  g_opll;
u8  g_bgm = SND_NONE;
s8  g_snd_trans, g_snd_bend, g_snd_detune;
u8  g_snd_tempo = 128;
u8  g_snd_atten;
u16 g_snd_mute;
u8  g_snd_bar;
u16 g_snd_keyons;
u16 g_pcm_starts;
u8  g_pcm_cur = SND_NONE;
u8  g_snd_room;                      /* main: 前のフレームの空き時間が平常の半分以上 */

typedef struct {
    u8 *p;          /* 次の命令(0 = 止まっている) */
    s16 wait;       /* 次の命令までの刻み */
    s16 off;        /* 音を切るまでの刻み(0 以下 = 切った / 切らない) */
    u8  note;       /* 鳴らしている音(移調前。0xFF = 無し) */
    u8  vol, inst, cut, flags;
    s8  det;
    u8  env, envt;  /* PSG: 今の音量、エンベロープの時計 */
} Ch;
#define F_ON     1  /* 鳴っている(キーオン中) */
#define F_LEG    2  /* 次の音をつなぐ */
#define F_NOISE  4  /* PSG: ノイズ */
Ch ch[SND_NCH];                      /* ★asm から参照するので static にしない */

u16 rate;                            /* 曲のテンポ(刻み × 256 / フレーム) */
static u16 acc;
static u8  song_bank, song_next;
static u8  map_a8;
static u8  map_bank;
u16 last_jiffy;
u8  psg_mix = 0;                     /* 有効にしているもの(bit0..2 音 A..C、bit3..5 ノイズ A..C) */
u8  psg_mix_w = 0xFF;                /* PSG R#7 に書いた値 */
static s8  bend_w, detune_w;         /* 鳴っている音に反映済みの値 */
static u8  atten_w;

/* ---- 書き込み ---- */
/* ★OPLL の待ち(アドレス後 3.36us、データ後 23.52us)。R800 の RAM 実行で 1 周約 0.5us。ROM 実行なら長くなる(安全側) */
void opll_w(u8 reg, u8 val) __naked {
    (void)reg; (void)val;
    __asm
        push bc                    ; ★B, C, DE, HL を壊さない(シーケンサの約束)
        ld   b, a
        ld   a, (_opll_cnt)
        inc  a
        ld   (_opll_cnt), a
        ld   a, (_g_opll)
        or   a
        jr   z, 00003$             ; MSX-MUSIC が無ければ書かない
        ld   a, b
        out  (0x7C), a
        ld   b, #8
    00001$:
        djnz 00001$
        ld   a, l
        out  (0x7D), a
        ld   b, #52
    00002$:
        djnz 00002$
    00003$:
        pop  bc
        ret
    __endasm;
}

void psg_w(u8 reg, u8 val) {
    PSG_A = reg;
    PSG_W = val;
}

void psg_mix_apply(void) {
    u8 v = (u8)(0x80 | (~psg_mix & 0x3F));
    if (v != psg_mix_w) { psg_mix_w = v; psg_w(7, v); }
}

/* ---- ページ 2 に曲のバンクを出す / 戻す ---- */
static void snd_map(void) __naked {
    __asm
        in   a, (0xA8)
        ld   (_map_a8), a
        ld   b, a
        ld   a, (_ramx_a8)
        and  #0x3C
        ld   c, a
        ld   a, b
        and  #0xC3
        or   c
        out  (0xA8), a             ; ページ 1-2 = カートリッジ(ページ 1 は RAM の写しと同じコード)
        ld   a, (_map_bank)
        ld   (0x7000), a
        inc  a
        ld   (0x7800), a
        ld   a, (_ramx_a8)
        and  #0x30
        ld   c, a
        ld   a, (_map_a8)
        and  #0xCF
        or   c
        out  (0xA8), a             ; ページ 1 = RAM に戻し、ページ 2 だけカートリッジ
        ret
    __endasm;
}

static void snd_unmap(void) __naked {
    __asm
        ld   a, (_ramx_a8)
        and  #0x3C
        ld   c, a
        ld   a, (_map_a8)
        and  #0xC3
        or   c
        out  (0xA8), a
        ld   a, #2
        ld   (0x7000), a           ; 窓をコードのバンクへ戻す
        inc  a
        ld   (0x7800), a
        ld   a, (_map_a8)
        out  (0xA8), a
        ret
    __endasm;
}

/* ---- 音程 ---- */
/* OPLL の F-Number(ブロック内、C..B)と PSG の周期(o1、C..B。1.7897725MHz / 16 / f) */
const u16 fm_fnum[12] = { 172, 182, 193, 204, 217, 230, 243, 258, 273, 289, 306, 325 };
const u16 psg_per1[12] = { 3421, 3229, 3048, 2877, 2715, 2563, 2419, 2283, 2155, 2034, 1920, 1812 };

/* PSG のエンベロープ(音色の番号): 何フレームごとに 1 下がるか(0 = 下がらない)、下限 */
static const u8 env_rate[8] = { 0, 1, 2, 4, 8, 16, 3, 6 };
static const u8 env_floor[8] = { 0, 0, 0, 3, 6, 8, 0, 4 };

u8 sfx_t;                            /* 効果音の残りフレーム(0 = 鳴っていない。PSG C を使っている) */
u8 seq_n;                            /* seq_run: 進める刻み数 */
u8 seq_need;                         /* seq_run の結果: 命令を読むチャンネルがある */
u8 opll_cnt;                         /* OPLL へ書いた回数(seq_events の予算) */
u8 opll_budget;
u8 retune_f, retune_v;               /* retune: 音程 / 音量を書き直す */
u8 pend = SND_NONE;
u8 pend_bar;

/* ---- シーケンサの中核(asm。速さと大きさのため) ----
   チャンネルの構造体 Ch: +0 p, +2 wait, +4 off, +6 note, +7 vol, +8 inst, +9 cut, +10 flags, +11 det, +12 env, +13 envt
   ★下請けの約束: IX = チャンネル、C = チャンネル番号(0..9)。下請けは C と IX を壊さない。
     _opll_w(A = レジスタ, L = 値)は B, C, DE, HL を壊さない。 */
static void seq_core(void) __naked {
    __asm
        ; ---- seq_run: 全チャンネルを seq_n 刻み進める ----
    _seq_run::
        xor  a
        ld   (_seq_need), a
        push ix
        ld   ix, #_ch
        ld   c, #0
    sr_loop:
        ld   a, 1 (ix)
        or   a
        jp   z, sr_next            ; p == 0(曲データは 0x8000 から)
        ld   a, 5 (ix)
        cp   #0x7F
        jr   nz, sr_off
        ld   a, 4 (ix)
        inc  a
        jr   z, sr_wait            ; off = 0x7FFF: 切らない
    sr_off:
        ld   l, 4 (ix)
        ld   h, 5 (ix)
        ld   a, (_seq_n)
        ld   e, a
        ld   d, #0
        or   a
        sbc  hl, de
        jr   z, sr_offnow
        jp   m, sr_offnow
        ld   4 (ix), l
        ld   5 (ix), h
        jr   sr_wait
    sr_offnow:
        ld   4 (ix), #0xFF
        ld   5 (ix), #0x7F
        call key_off
    sr_wait:
        ld   l, 2 (ix)
        ld   h, 3 (ix)
        ld   a, (_seq_n)
        ld   e, a
        ld   d, #0
        or   a
        sbc  hl, de
        ld   2 (ix), l
        ld   3 (ix), h
        jr   z, sr_run
        jp   p, sr_next
    sr_run:
        ld   a, #1
        ld   (_seq_need), a        ; 命令を読むチャンネルがある(曲のバンクを出して seq_events)
    sr_next:
        ld   de, #14
        add  ix, de
        inc  c
        ld   a, c
        cp   #10
        jp   nz, sr_loop
        pop  ix
        ret

        ; ---- _seq_events: 待ちが 0 以下のチャンネルの命令を読む(★曲のバンクを出している間に) ----
    _seq_events::
        xor  a
        ld   (_opll_cnt), a
        push ix
        ld   ix, #_ch
        ld   c, #0
    se_loop:
        ld   a, (_opll_cnt)
        ld   hl, #_opll_budget
        cp   (hl)                  ; ★1 フレームに OPLL へ書くのは 12 回まで(1 回約 30us。空きの無いフレームの後は 6 回)。小節の頭は全チャンネルが
        jr   nc, se_end            ;   一度に鳴り、重い場面と重なって遅れた。残りのチャンネルは次のフレームで(時刻は保つ)
        ld   a, 1 (ix)
        or   a
        jr   z, se_next
        ld   a, 3 (ix)
        bit  7, a
        jr   nz, se_run
        or   2 (ix)
        jr   nz, se_next
    se_run:
        call ch_run
    se_next:
        ld   de, #14
        add  ix, de
        inc  c
        ld   a, c
        cp   #10
        jr   nz, se_loop
    se_end:
        pop  ix
        ret

        ; ---- ch_run: 待ちが正になるまで命令を読む ----
    ch_run:
        ld   l, 0 (ix)
        ld   h, 1 (ix)
    cr_loop:
        ld   a, 3 (ix)
        bit  7, a
        jr   nz, cr_ev
        or   2 (ix)
        jr   z, cr_ev
        ld   0 (ix), l
        ld   1 (ix), h
        ret
    cr_ev:
        ld   a, (hl)
        inc  hl
        cp   #0x60
        jr   nc, cr_cmd
        ld   b, a                  ; 音
        ld   a, (hl)               ; 長さ
        inc  hl
        push hl
        push af
        ld   a, b
        call note_on
        pop  af
        call add_wait
        ld   b, a
        ld   a, 9 (ix)             ; cut
        or   a
        jr   z, cr_nooff
        cp   b
        jr   nc, cr_nooff
        ld   e, a
        ld   d, #0
        ld   l, 2 (ix)
        ld   h, 3 (ix)
        or   a
        sbc  hl, de
        ld   4 (ix), l
        ld   5 (ix), h
        pop  hl
        jr   cr_loop
    cr_nooff:
        ld   4 (ix), #0xFF
        ld   5 (ix), #0x7F
        pop  hl
        jr   cr_loop
    cr_cmd:
        jr   nz, cr_c61
        push hl                    ; 0x60 休み
        call key_off
        pop  hl
    cr_len:
        ld   a, (hl)
        inc  hl
        call add_wait
        jr   cr_loop
    cr_c61:
        cp   #0x61
        jr   z, cr_len
        cp   #0x90
        jr   nc, cr_c90
        and  #15
        ld   7 (ix), a
        jr   cr_loop
    cr_c90:
        cp   #0xA0
        jr   nc, cr_ca0
        and  #15
        ld   8 (ix), a
        jr   cr_loop
    cr_ca0:
        jr   nz, cr_ca1
        ld   a, (hl)
        inc  hl
        ld   9 (ix), a
        jp   cr_loop
    cr_ca1:
        cp   #0xA1
        jr   nz, cr_ca2
        ld   a, (hl)
        inc  hl
        ld   11 (ix), a
        jp   cr_loop
    cr_ca2:
        cp   #0xA2
        jr   nz, cr_ca3
        set  1, 10 (ix)
        jp   cr_loop
    cr_ca3:
        cp   #0xA3
        jr   nz, cr_ca4
        ld   a, (hl)
        inc  hl
        push hl
        push bc
        call _pcm_req_seq          ; 曲の中の PCM: 声は必ず、ほかは任意(CPU に余裕があるときだけ)
        pop  bc
        pop  hl
        jp   cr_loop
    cr_ca4:
        cp   #0xA4
        jr   nz, cr_ca5
        ld   a, (_g_snd_bar)
        inc  a
        ld   (_g_snd_bar), a
        ld   a, (_pend)
        inc  a
        jp   z, cr_loop
        ld   a, #1
        ld   (_pend_bar), a
        jp   cr_loop
    cr_ca5:
        cp   #0xA5
        jr   nz, cr_ca6
        ld   a, (hl)
        inc  hl
        push hl
        call noise_set
        pop  hl
        jp   cr_loop
    cr_ca6:
        cp   #0xA6
        jr   nz, cr_cfe
        ld   a, (hl)
        inc  hl
        ld   (_rate), a
        ld   a, (hl)
        inc  hl
        ld   (_rate + 1), a
        jp   cr_loop
    cr_cfe:
        cp   #0xFE
        jr   nz, cr_end
        ld   a, (hl)
        inc  hl
        ld   h, (hl)
        ld   l, a
        jp   cr_loop
    cr_end:
        call key_off               ; 0xFF: 終わり
        xor  a
        ld   0 (ix), a
        ld   1 (ix), a
        ret

    add_wait:                      ; wait += A(A は保つ)
        push hl
        ld   e, a
        ld   d, #0
        ld   l, 2 (ix)
        ld   h, 3 (ix)
        add  hl, de
        ld   2 (ix), l
        ld   3 (ix), h
        pop  hl
        ret

        ; ---- PSG のノイズ(A = 0 音 / 1 ノイズ) ----
    noise_set:
        ld   b, a
        ld   a, c
        sub  #7
        ld   e, #1
        jr   z, ns2
    ns1:
        sla  e
        dec  a
        jr   nz, ns1
    ns2:
        ld   a, e
        rlca
        rlca
        rlca
        ld   d, a                  ; D = ノイズのビット、E = 音のビット
        ld   a, b
        or   a
        jr   z, ns_off
        set  2, 10 (ix)
        ld   a, e
        cpl
        ld   b, a
        ld   a, (_psg_mix)
        and  b
        or   d
        ld   (_psg_mix), a
        ret
    ns_off:
        res  2, 10 (ix)
        ld   a, d
        cpl
        ld   b, a
        ld   a, (_psg_mix)
        and  b
        or   e
        ld   (_psg_mix), a
        ret

        ; ---- key_off ----
    key_off:
        bit  0, 10 (ix)
        ret  z
        res  0, 10 (ix)
        ld   a, c
        cp   #6
        jr   nc, ko_psg
        add  a, #0x20
        ld   l, #0
        jp   _opll_w               ; ★ブロックごと 0(余韻は音色の減衰)
    ko_psg:
        ret  z
        ld   12 (ix), #0
        ld   a, c
        sub  #7
        ld   l, #0
        ; そのまま _psg_vol へ

        ; ---- _psg_vol(A = j, L = 音量)。効果音が C を使っている間は書かない。全体の減衰を引く ----
    _psg_vol::
        ld   b, a
        cp   #2
        jr   nz, pv1
        ld   a, (_sfx_t)
        or   a
        ret  nz
    pv1:
        ld   a, (_g_snd_atten)
        ld   e, a
        ld   a, l
        sub  e
        jr   nc, pv2
        xor  a
    pv2:
        ld   l, a
        ld   a, b
        add  a, #8
        out  (0xA0), a
        ld   a, l
        out  (0xA1), a
        ret

        ; ---- note_on(A = 音) ----
    note_on:
        ld   b, a
        push bc
        ld   hl, (_g_snd_mute)
        ld   a, c
        or   a
        jr   z, no_m1
    no_m0:
        srl  h
        rr   l
        dec  a
        jr   nz, no_m0
    no_m1:
        bit  0, l
        pop  bc
        jp   nz, key_off           ; 消音中のチャンネル
        ld   hl, (_g_snd_keyons)
        inc  hl
        ld   (_g_snd_keyons), hl
        ld   a, c
        cp   #6
        jr   nz, no_nr
        ld   a, #0x0E              ; リズム: 打楽器のビット
        ld   l, #0x20
        call _opll_w
        ld   a, b
        and  #0x1F
        or   #0x20
        ld   l, a
        ld   a, #0x0E
        jp   _opll_w
    no_nr:
        ld   6 (ix), b
        jr   nc, no_psg
        ld   a, 10 (ix)
        and  #3
        cp   #3
        jr   z, no_leg             ; つなぐ: 音程だけ
        bit  0, 10 (ix)
        jr   z, no_fm1
        ld   a, c
        add  a, #0x20
        ld   l, #0
        call _opll_w               ; 鳴っていれば一度切る
    no_fm1:
        call fm_volreg
        ld   a, l
        cp   12 (ix)               ; FM: +12 = 書いた音色・音量(同じなら書かない。1 回約 30us)
        jr   z, no_leg
        ld   12 (ix), a
        ld   a, c
        add  a, #0x30
        call _opll_w
    no_leg:
        ld   e, #0x10
        call fm_freq
        jr   no_done
    no_psg:
        call psg_freq_c
        ld   a, 10 (ix)
        and  #3
        cp   #3
        jr   z, no_done
        ld   a, 7 (ix)
        ld   12 (ix), a
        ld   13 (ix), #0
        ld   l, a
        ld   a, c
        sub  #7
        call _psg_vol
    no_done:
        ld   a, 10 (ix)
        and  #0xFD
        or   #1
        ld   10 (ix), a
        ret

        ; ---- fm_volreg: L = 音色 << 4 | 減衰 ----
    fm_volreg:
        ld   a, (_g_snd_atten)
        add  a, #15
        sub  7 (ix)
        cp   #16
        jr   c, fv1
        ld   a, #15
    fv1:
        ld   l, a
        ld   a, 8 (ix)
        rlca
        rlca
        rlca
        rlca
        and  #0xF0
        or   l
        ld   l, a
        ret

        ; ---- eff_note: A = 音 + 移調 + ベンド(0..95) ----
    eff_note:
        ld   a, (_g_snd_trans)
        ld   e, a
        ld   a, (_g_snd_bend)
        add  a, e
        ld   e, a
        ld   a, 6 (ix)
        add  a, e
        bit  7, e
        jr   z, en_pos
        ret  c
        xor  a
        ret
    en_pos:
        cp   #96
        ret  c
        ld   a, #95
        ret

        ; ---- add_s8_de: DE += A(符号つき) ----
    add_s8_de:
        ld   l, a
        rlca
        sbc  a, a
        ld   h, a
        add  hl, de
        ex   de, hl
        ret

        ; ---- fm_freq(E = 0x10 でキーオン) ----
    fm_freq:
        push de
        call eff_note
        ld   b, #0
    ff1:
        cp   #12
        jr   c, ff2
        sub  #12
        inc  b
        jr   ff1
    ff2:
        add  a, a
        ld   e, a
        ld   d, #0
        ld   hl, #_fm_fnum
        add  hl, de
        ld   e, (hl)
        inc  hl
        ld   d, (hl)
        ld   a, 11 (ix)
        call add_s8_de
        ld   a, (_g_snd_detune)
        call add_s8_de
        ld   a, c
        add  a, #0x10
        ld   l, e
        call _opll_w
        ld   a, b
        cp   #8
        jr   c, ff3
        ld   a, #7
    ff3:
        add  a, a
        ld   l, a
        ld   a, d
        and  #1
        or   l
        pop  de
        or   e
        ld   l, a
        ld   a, c
        add  a, #0x20
        jp   _opll_w

        ; ---- psg_freq_c ----
    psg_freq_c:
        ld   a, c
        sub  #7
        ld   b, a
        cp   #2
        jr   nz, pf1
        ld   a, (_sfx_t)
        or   a
        ret  nz
    pf1:
        call eff_note
        bit  2, 10 (ix)
        jr   z, pf_tone
        and  #31
        cpl
        and  #31
        ld   l, a
        ld   a, #6
        out  (0xA0), a
        ld   a, l
        out  (0xA1), a
        ret
    pf_tone:
        ld   d, #0
    pf2:
        cp   #12
        jr   c, pf3
        sub  #12
        inc  d
        jr   pf2
    pf3:
        add  a, a
        ld   e, a
        push de
        ld   d, #0
        ld   hl, #_psg_per1
        add  hl, de
        ld   e, (hl)
        inc  hl
        ld   d, (hl)
        ex   de, hl
        pop  de
    pf4:
        ld   a, d
        cp   #2
        jr   c, pf5
        srl  h
        rr   l
        dec  d
        jr   pf4
    pf5:
        ex   de, hl
        ld   a, 11 (ix)
        call add_s8_de
        ld   a, (_g_snd_detune)
        call add_s8_de
        ld   a, b
        add  a, a
        out  (0xA0), a
        ld   a, e
        out  (0xA1), a
        ld   a, b
        add  a, a
        inc  a
        out  (0xA0), a
        ld   a, d
        and  #15
        out  (0xA1), a
        ret

        ; ---- _retune: 鳴っている音へ音程(retune_f)・音量(retune_v)を反映 ----
    _retune::
        push ix
        ld   ix, #_ch
        ld   c, #0
    rt_loop:
        bit  0, 10 (ix)
        jr   z, rt_next
        ld   a, c
        cp   #6
        jr   z, rt_next
        jr   nc, rt_psg
        ld   a, (_retune_v)
        or   a
        jr   z, rt_f
        call fm_volreg
        ld   12 (ix), l
        ld   a, c
        add  a, #0x30
        call _opll_w
    rt_f:
        ld   a, (_retune_f)
        or   a
        jr   z, rt_next
        ld   e, #0x10
        call fm_freq
        jr   rt_next
    rt_psg:
        ld   a, (_retune_f)
        or   a
        call nz, psg_freq_c
    rt_next:
        ld   de, #14
        add  ix, de
        inc  c
        ld   a, c
        cp   #10
        jr   nz, rt_loop
        pop  ix
        ret

        ; ---- _all_keyoff: 全チャンネルを切って止める ----
    _all_keyoff::
        push ix
        ld   ix, #_ch
        ld   c, #0
    ak1:
        call key_off
        xor  a
        ld   0 (ix), a
        ld   1 (ix), a
        ld   10 (ix), a
        ld   de, #14
        add  ix, de
        inc  c
        ld   a, c
        cp   #10
        jr   nz, ak1
        pop  ix
        ret
    __endasm;
}
void seq_run(void);
void seq_events(void);
void retune(void);
void all_keyoff(void);
void psg_vol(u8 j, u8 v);

/* 曲の頭を読んでチャンネルを張る(★曲のバンクを出している間に呼ぶ) */
static void seq_start(u16 addr) {
    u8 *h = (u8 *)addr, i;
    Ch *c = ch;
    rate = (u16)(h[0] | (h[1] << 8));
    song_next = h[2];
    for (i = 0; i < 8; i++) opll_w(i, h[3 + i]);
    for (i = 0; i < SND_NCH; i++, c++) {
        u16 a = (u16)(h[11 + i * 2] | (h[12 + i * 2] << 8));
        c->p = (u8 *)a;
        c->wait = 0; c->off = 0x7FFF; c->note = 0xFF; c->vol = 12; c->inst = 1; c->cut = 0; c->det = 0;
        c->flags &= F_ON;
        c->env = 0xFF;                            /* FM: 書いた音量(まだ無い) */
    }
    acc = 0x100;                                  /* 頭の音はこのフレームで出す */
}

static void all_off(void) {
    all_keyoff();
    opll_w(0x0E, 0x20);
    psg_mix &= 0x24;                             /* C は効果音のもの */
    psg_mix_apply();
}

static void start_song(u8 id) {
    all_off();
    g_bgm = id;
    pend = SND_NONE; pend_bar = 0;
    if (id == SND_NONE || id >= SND_NSONG) { g_bgm = SND_NONE; return; }
    map_bank = song_bank = snd_song_bank[id];
    psg_mix = (u8)((psg_mix & 0x24) | 0x03);     /* A, B は音 */
    if (!sfx_t) psg_mix |= 0x04;
    psg_mix_apply();
    snd_map();
    seq_start(snd_song_addr[id]);
    snd_unmap();
    bend_w = g_snd_bend; detune_w = g_snd_detune; atten_w = g_snd_atten;
}

/* ★曲の頭は全チャンネルが一度に鳴り、OPLL へ数十回書く(1 回約 30us)。場面の入りの重いフレームと重ならないよう、
   空き時間のあるフレームまで最大 30 フレーム待って始める(snd_frame) */
static u8 play_req = 0xFE, play_wait;
void bgm_play(u8 id) { play_req = id; play_wait = 0; }
void bgm_next(u8 id) { if (g_bgm == SND_NONE) bgm_play(id); else pend = id; }
void bgm_stop(void) { start_song(SND_NONE); }

/* PSG のエンベロープ(毎フレーム) */
static void psg_env(void) {
    u8 j;
    for (j = 0; j < 3; j++) {
        Ch *c = &ch[7 + j];
        u8 r;
        if (!(c->flags & F_ON) || !c->env) continue;
        r = env_rate[c->inst & 7];
        if (!r || c->env <= env_floor[c->inst & 7]) continue;
        if (++c->envt < r) continue;
        c->envt = 0;
        c->env--;
        psg_vol(j, c->env);
    }
}

/* ---- 効果音(PSG C。本体は music.c = CODE0) ---- */
void sfx_frame(void);
u8 sfx_c9(void) { return (u8)((ch[9].p ? 1 : 0) | ((ch[9].flags & F_NOISE) ? 2 : 0)); }

/* ---- PCM: 頼む側(曲を読んでいる間にも呼ばれるのでページ 1 に置く)。鳴らす側は music.c(CODE0) ---- */
u8  pcm_q = SND_NONE;                         /* 頼まれた PCM(bit7 = 任意のもの) */
u8  quiet;                                    /* 余裕のあるフレームが続いた数(pcm_keep) */
u8  pcm_opt;                                  /* 鳴っている PCM は任意のもの */
u8  pcm_prio;

/* 曲の中の PCM: 声は必ず、ほかは任意 */
void pcm_req_seq(u8 id) {
    if (snd_pcm_prio[id] >= 4) pcm_req(id); else pcm_req_opt(id);
}

/* 任意の PCM(4 小節ごとのオケヒ、爆発など): ★刻み方式は CPU を約 4 割使うので、余裕のあるフレームが 1 秒続いているときだけ */
void pcm_req_opt(u8 id) {
    if (quiet >= 60 && (g_pcm_cur == SND_NONE || pcm_opt)) { pcm_req(id); pcm_q |= 0x80; }
}

u8  g_pcm_off;                                /* 検証用: 1 = PCM を鳴らさない(m1..m6 はエンジンの時間を見るので切る) */
u16 g_pcm_cuts;                               /* 遅れたので PCM を止めた回数(m7 が遅れと突き合わせる) */
u8  g_pcm_drops;                              /* 落ち着かずに鳴らさなかった回数(m7) */

void pcm_req(u8 id) {
    if (id >= SND_NPCM || g_pcm_off) return;
    if (g_pcm_cur != SND_NONE && snd_pcm_prio[id] < pcm_prio) return;
    pcm_q = id;                               /* 始めるのは pcm_keep(フレームの頭) */
}

/* ---- 毎フレーム ---- */
#define JIFFY (*(volatile u16 *)0xFC9E)

void snd_frame(void) {
    u16 j = JIFFY, el = (u16)(j - last_jiffy);
    last_jiffy = j;
    if (el > 8) el = 8;                           /* 長く止まった後はまとめて進めない(先送りは 5 フレームまで) */
    if (g_bgm != SND_NONE) {
        u8 n;
        u16 r = rate;
        if (g_snd_tempo != 128) r = (u16)(((r >> 2) * g_snd_tempo) >> 5);
        while (el--) acc += r;
        n = (u8)(acc >> 8);
        acc &= 0xFF;
        if (n) {
            seq_n = n;
            seq_run();                            /* 待ちを減らす(曲のバンクは要らない) */
            if (seq_need) {                       /* ★バンクの出し入れは 1 回約 50us。命令を読むフレームだけ */
                opll_budget = g_snd_room ? 12 : 6;
                map_bank = song_bank;
                snd_map();
                seq_events();
                snd_unmap();
            }
        }
        if (pend_bar || (pend != SND_NONE && !ch[0].p && !ch[6].p && !ch[7].p)) start_song(pend);
        else {
            u8 i, live = 0;
            for (i = 0; i < SND_NCH; i++) if (ch[i].p) live = 1;
            if (!live) start_song(song_next);     /* 曲が終わった: 続きの曲(無ければ止まる) */
        }
    }
    if (play_req != 0xFE && (g_snd_room || ++play_wait >= 30)) { start_song(play_req); play_req = 0xFE; }
    retune_f = (u8)(g_snd_bend != bend_w || g_snd_detune != detune_w);
    retune_v = (u8)(g_snd_atten != atten_w);
    if (retune_f | retune_v) {
        bend_w = g_snd_bend; detune_w = g_snd_detune; atten_w = g_snd_atten;
        retune();
    }
    psg_env();
    sfx_frame();
    psg_mix_apply();
}

void snd_silence(void) {
    sfx_t = 0;
    psg_w(10, 0);
    start_song(SND_NONE);
    pcm_halt();
}


/* ---- MSX-MUSIC を探す(内蔵 APRLOPLL を先に。前作 §16-37)。★BIOS の RDSLT を使う ---- */
static u8 det_slot;
static u16 det_addr;
static u8 rdslt(void) __naked {
    __asm
        ld   a, (_det_slot)
        ld   hl, (_det_addr)
        call 0x000C
        ret
    __endasm;
}

void snd_detect(void) {
    static const char sig[] = "APRLOPLL";
    u8 ps, ss, k;
    g_opll = 0;
    for (ps = 0; ps < 4 && !g_opll; ps++) {
        u8 ex = (u8)(*(volatile u8 *)(0xFCC1 + ps) & 0x80);
        for (ss = 0; ss < (ex ? 4 : 1) && !g_opll; ss++) {
            det_slot = (u8)(ex | (ss << 2) | ps);
            for (k = 0; k < 8; k++) {
                det_addr = (u16)(0x4018 + k);
                if (rdslt() != (u8)sig[k]) break;
            }
            if (k == 8) g_opll = 1;
        }
    }
    __asm ei __endasm;
}

/* ★このファイルの最後。番地がページ 1(0x8000 未満)にあることを Makefile が検査する */
void snd_end(void) { }
