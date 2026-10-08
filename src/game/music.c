/* music.c — 音源の初期化、PCM を鳴らす側、PSG の効果音(指示書 6)。選曲と場面の音の演出は第 2 バンクの music_sel.c。
   ★CODE0(ページ 0 の RAM)に置く(Makefile)。常駐(ページ 1-2)に空きが無いため。CODE0 は起動の途中で複製するので、
     本体のループ(music_frame / sfx)からだけ呼ぶ。

   ★sfx / pcm_req は第 2 バンクの中からも呼ぶので CODE0 に置く(呼び口にすると入れ子になる)。 */
#include "game.h"
#include "snd.h"
#include "snd_data.h"

/* 場面に合わせて曲を選ぶ部分(music_frame)は第 2 バンクの music_sel.c(CODE0 の空きが尽きた) */

extern u8 sfx_t, psg_mix;
void psg_w(u8 reg, u8 val);
void psg_mix_apply(void);

/* ---- 音源の初期化(起動時に 1 回。CODE0 を複製した後) ---- */
__sfr __at(0xA5) PCM_CTRL;
extern u8 psg_mix_w;
extern u16 last_jiffy;
void opll_w(u8 reg, u8 val);
void snd_init(void) {
    u8 i;
    if (g_opll) {
        for (i = 0; i < 0x39; i++) opll_w(i, 0);
        opll_w(0x16, 0x20); opll_w(0x17, 0x50); opll_w(0x18, 0xC0);   /* リズムの音程(定番の値) */
        opll_w(0x26, 0x05); opll_w(0x27, 0x05); opll_w(0x28, 0x01);
        opll_w(0x36, 0x02);                       /* BD */
        opll_w(0x37, 0x42);                       /* HH, SD */
        opll_w(0x38, 0x22);                       /* TOM, CYM */
        opll_w(0x0E, 0x20);                       /* リズムモード */
    }
    for (i = 8; i < 11; i++) psg_w(i, 0);
    psg_mix = 0; psg_mix_w = 0xFF;
    psg_mix_apply();
    PCM_CTRL = 0x03;                              /* D/A、BUFF = 1(書いた値は次の 15.7kHz の刻みで出る。方式 B の揺れをそろえる) */
    last_jiffy = *(volatile u16 *)0xFC9E;
}

/* ---- PCM を鳴らす側(ROM → RAM の 2 面 → 刻み方式)。頼む側は snd.c ---- */
#include "rom.h"
#include "raster.h"
/* PCM 方式 B の制御(割込みと送出口の側は snd.c の pcmx)。★常駐の空きのため CODE0 に置く */
extern u8 *px_ptr, *px_nptr, px_on;
extern u16 px_left, px_nleft, px_due;
void pcmx_isr(void);
static void pcmx_play(u8 *p, u16 n) {
    extern void (*volatile g_poll_hook)(void);
    extern void (*volatile g_vdp_wait_hook)(void);
    __asm di __endasm;
    px_ptr = p; px_left = n; px_nleft = 0;
    __asm
        in   a, (0xE6)             ; 予定 = 今(下位から読む。桁上がりをまたぐと 1ms 先になり、鳴り始めが 1ms 遅れるだけ)
        ld   (_px_due), a
        in   a, (0xE7)
        ld   (_px_due + 1), a
    __endasm;
    px_on = 1;
    g_poll_hook = pcmx_service; g_vdp_wait_hook = pcmx_service; ras_pcm_hook = pcmx_isr;
    __asm ei __endasm;
}
void pcmx_queue(u8 *p, u16 n); u8 pcmx_queued(void); u16 pcmx_left(void); void pcmx_stop(void);   /* snd.c */

#define PCM_BUF  ((u8 *)0x3D00)               /* ページ 0。ROTOR の作業域(..0x3C7F)の上 */
#define PCM_HALF 384
extern const u8 snd_pcm_bank[], snd_pcm_prio[];
extern const u16 snd_pcm_off[], snd_pcm_len[];
extern u8 pcm_q, quiet, pcm_opt, pcm_prio, g_snd_room;
static u8  pcm_bank;
static u16 pcm_off, pcm_left_src;
static u8  pcm_last;                          /* 最後に鳴らす予約をした面 */
static u16 pcm_qwait, late_seen;

void pcm_halt(void) {
    pcmx_stop();
    g_pcm_cur = SND_NONE;
    pcm_left_src = 0;
}

static u16 pcm_load(u8 half, u16 max) {
    u16 n = pcm_left_src > max ? max : pcm_left_src, i;
    u8 *d = PCM_BUF + (half ? PCM_HALF : 0);
    for (i = 0; i < n; i += 64) {                 /* ★64 バイトずつ写して間に PCM を出す(384 バイトを一度に写すと 2ms 途切れた。M7) */
        u16 c = n - i > 64 ? 64 : n - i;
        rom_to_ram(pcm_bank, pcm_off, d + i, c);
        pcm_off += c;
        pcmx_service();
    }
    pcm_left_src -= n;
    return n;
}

static void pcm_begin(u8 id) {
    g_pcm_cur = id;
    pcm_prio = snd_pcm_prio[id];
    pcm_bank = snd_pcm_bank[id];
    pcm_off = snd_pcm_off[id];
    pcm_left_src = snd_pcm_len[id];
    /* ★両方の面を詰めてから始める(初めを 128 バイトにしていたら 1 フレームで使い切り、次の面が来るまで止まって
       遅れが残った。写す間も 64 バイトごとに PCM を出す。M7) */
    pcmx_play(PCM_BUF, pcm_load(0, PCM_HALF));
    pcm_last = 0;
    if (pcm_left_src) { pcmx_queue(PCM_BUF + PCM_HALF, pcm_load(1, PCM_HALF)); pcm_last = 1; }
    g_pcm_starts++;
}

/* 毎フレーム(snd_frame を先送りしたフレームも) */
void pcm_keep(void) {
    /* ★方式 B(送出口でタイマを見て出す)は CPU をほとんど使わないので、頼まれたらすぐ鳴らす。
       方式 C(刻みの割込み)の頃の「余裕のあるときだけ」「遅れたら止める」は外した(M7) */
    if (pcm_q != SND_NONE) {
        pcm_qwait = 0;
        pcm_opt = (u8)(pcm_q >> 7);
        pcm_begin((u8)(pcm_q & 0x7F));
        pcm_q = SND_NONE;
        return;
    }
    if (g_pcm_cur == SND_NONE || pcmx_queued()) return;
    if (pcm_left_src) {
        u8 h = (u8)(pcm_last ^ 1);
        pcmx_queue(PCM_BUF + (h ? PCM_HALF : 0), pcm_load(h, PCM_HALF));
        pcm_last = h;
    } else if (!pcmx_left()) {
        pcm_halt();                               /* 鳴り終わった */
    }
}

/* ---- 効果音(PSG C) ---- */
typedef struct { u16 per; s8 dper; u8 vol, vdec, len, mix, noise, prio; } SfxDef;
static const SfxDef sfx_tab[SFX_COUNT] = {
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 60, 14, 11, 1, 7, 1, 0, 1 },            /* SHOT: 下がるレーザー */
    { 0, 0, 10, 1, 5, 2, 4, 2 },              /* HIT: 短いノイズ */
    { 300, 20, 13, 2, 14, 3, 10, 3 },         /* KILL */
    { 0, 0, 15, 4, 44, 2, 28, 5 },            /* BOOM: 長いノイズ */
    { 140, -8, 12, 2, 10, 1, 0, 2 },          /* ITEM: 上がる */
    { 100, -3, 14, 3, 30, 1, 0, 4 },          /* SHARD */
    { 220, -24, 13, 2, 12, 1, 0, 4 },         /* CONFIRM */
    { 400, -12, 15, 4, 48, 3, 20, 6 },        /* LIMIT BREAK */
    { 120, 10, 15, 4, 52, 3, 24, 6 },         /* DIE */
    { 0, 0, 12, 1, 2, 2, 1, 1 },              /* TICK: 時計 */
    { 900, 0, 10, 2, 8, 1, 0, 2 },            /* DENY */
};
u8 sfx_c9(void);                              /* snd.c: BGM の C の状態(bit0 = 使う、bit1 = ノイズ) */
static u8 sfx_id, sfx_v, sfx_vt;
static u16 sfx_per;

void sfx(u8 id) {
    const SfxDef *d = &sfx_tab[id];
    if (sfx_t && sfx_tab[sfx_id].prio > d->prio) return;
    sfx_id = id; sfx_t = d->len; sfx_v = d->vol; sfx_vt = 0; sfx_per = d->per;
    psg_mix = (u8)((psg_mix & ~0x24) | ((d->mix & 1) ? 0x04 : 0) | ((d->mix & 2) ? 0x20 : 0));
    psg_mix_apply();
    if (d->mix & 2) psg_w(6, d->noise);
}

void sfx_frame(void) {
    const SfxDef *d;
    if (!sfx_t) return;
    d = &sfx_tab[sfx_id];
    if (d->mix & 1) { psg_w(4, (u8)sfx_per); psg_w(5, (u8)((sfx_per >> 8) & 15)); sfx_per = (u16)(sfx_per + d->dper); }
    psg_w(10, sfx_v);
    if (++sfx_vt >= d->vdec) { sfx_vt = 0; if (sfx_v) sfx_v--; }
    if (--sfx_t == 0) {
        u8 c9 = sfx_c9();
        psg_w(10, 0);
        psg_mix &= (u8)~0x24;
        if (c9 & 1) psg_mix |= (c9 & 2) ? 0x20 : 0x04;            /* BGM の C に返す */
        psg_mix_apply();
    }
}
