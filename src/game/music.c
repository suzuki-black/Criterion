/* music.c — 場面に合わせて曲を選ぶ(指示書 6.3)、場面の音の演出、PSG の効果音。
   ★CODE0(ページ 0 の RAM)に置く(Makefile)。常駐(ページ 1-2)に空きが無いため。CODE0 は起動の途中で複製するので、
     本体のループ(music_frame / sfx)からだけ呼ぶ。

   区間(g_sec)は全面の通し番号なので、区間 → 曲の表を 1 つ持てば各面のオーバレイは曲を知らなくてよい。
   空中戦(M03)は面ごとに移調し、面の本体に入ると次の小節の頭で面の曲へ切り替える。ボスは警告(J02)から。 */
#include "game.h"
#include "snd.h"
#include "snd_data.h"

#define A_  0xF0   /* 空中戦(M03、面ごとに移調) */
#define S_  0xF1   /* 面の曲 */
#define MI  0xF2   /* 中ボス(M12) */
#define BO  0xF3   /* ボス(J02 → M13) */
#define OU  0xF4   /* クリア(J01) */
#define ST  0xF5   /* 止める(5 面の撃沈) */
#define NSEC 69
static const u8 sec_song[NSEC] = {
    A_, S_, S_, MI, S_, BO, OU,                              /* 1 面  0.. 6 */
    A_, S_, S_, S_, MI, S_, BO, OU,                          /* 2 面  7..14 */
    A_, S_, S_, S_, S_, MI, S_, BO, OU,                      /* 3 面 15..23 */
    A_, S_, S_, S_, S_, MI, S_, BO, OU,                      /* 4 面 24..32 */
    A_, S_, S_, S_, S_, S_, S_, S_, BO, ST, OU,              /* 5 面 33..43 */
    A_, S_, S_, S_, MI, S_, BO, OU,                          /* 6 面 44..51 */
    S_, BO, OU,                                              /* 7 面 52..54 */
    A_, S_, S_, MI, S_, SONG_M14A, SONG_M14B, SONG_M14C, SONG_M15, SONG_M15,   /* 8 面 55..64 */
    SONG_M16, SONG_M16, SONG_M16, SONG_M16,                  /* 終幕 65..68 */
};
static const u8 sec_first[9] = { 7, 15, 24, 33, 44, 52, 55, 65, 255 };   /* 次の面の最初の区間 */
static const u8 stage_song[8] = { SONG_M04, SONG_M05, SONG_M06, SONG_M07, SONG_M08, SONG_M09, SONG_M10, SONG_M11 };
static const s8 air_trans[8] = { 0, 2, 3, 4, 5, 6, 7, 8 };     /* 空中戦は面ごとに上がっていく */

extern s8 snd_trans_next;
static u8  want = 0xFE, sec_prev = 0xFF, ph_prev = 0xFF, bs_prev, calib_prev;
static u16 mt;                                   /* 区間に入ってからのフレーム */
static u8  v04;                                  /* 8 面の声を鳴らした */

/* 区間の中の音の演出(毎フレーム、値が変わったときだけドライバが書き直す) */
static void effects(u8 sec) {
    s8 bend = 0, det = 0;
    u8 tempo = 128, atten = 0;
    u16 mute = 0;
    if (sec == 10) {                                             /* 2 面 重力反転: 半音ずつ沈んで戻る */
        if (mt < 40) bend = (s8)-(s8)(mt >> 3);
        else if (mt < 80) bend = (s8)-(s8)((80 - mt) >> 3);
    } else if (sec >= 15 && sec <= 23) {                         /* 3 面: テンポをスクロールの速さに */
        s8 v = g_scroll_speed < 0 ? (s8)-g_scroll_speed : g_scroll_speed;
        if (v > 4) { v = (s8)((v - 4) >> 1); tempo = (u8)(128 + (v > 12 ? 12 : v)); }
        if (sec == 18 && !g_scroll_speed) atten = 15;            /* 急停止: 無音 */
        if (sec == 20 && g_boss_state == BS_DYING) bend = (s8)((mt & 4) ? 1 : -1);   /* 錘が壊れて曲がねじれる */
    } else if (sec == 25) mute = 0x0182;                         /* 4 面: 帯が増えるほどパートが増える */
    else if (sec == 26) mute = 0x0180;
    else if (sec == 27) mute = 0x0100;
    else if (sec == 28) det = (s8)((mt & 32) ? 2 : -2);          /* 陽炎帯: ゆらぐ */
    else if (sec == 47) mute = 0x0007;                           /* 6 面 影: 高い声部が消える */
    else if (sec == 57) det = (s8)(((mt >> 3) & 3) - 1);         /* 8 面 歪曲回廊: ピッチがゆらぐ */
    else if (sec == 67) {                                        /* 終幕: スタッフロールで楽器が減っていく */
        static const u16 drop[6] = { 0, 0x0100, 0x0104, 0x0184, 0x0186, 0x018E };
        u8 k = (u8)(mt >> 9);
        mute = drop[k > 5 ? 5 : k];
    }
    if (sec == 42 && mt == 45) pcm_req_opt(PCM_P03);                 /* 5 面 撃沈: 爆発の後に低いオケヒ */
    g_snd_bend = bend; g_snd_detune = det; g_snd_tempo = tempo; g_snd_atten = atten; g_snd_mute = mute;
}

static void on_sec(u8 sec) {
    if (sec == 52) v04 = 0;
    if ((sec == 54 || sec == 56 || sec == 59 || sec == 62) && !v04) pcm_req(PCM_V04);   /* 8 面: UNMEASURABLE。幕開け(STAGE 8 の表示の間)に。
                                                                     8 面は重く、落ち着かずに捨てたら縦穴・中枢・第 3 形態で頼み直す */
    else if (sec == 10 || sec == 19) pcm_req_opt(PCM_P04);           /* 重力反転 / 逆走: 逆再生シンバル */
    else if (sec == 18) pcm_req_opt(PCM_P03);                        /* 急停止: オケヒ 1 発 */
}

void music_frame(u8 over) {
    u8 w, st = 0, sec = g_sec;
    if (sec >= NSEC) sec = NSEC - 1;
    while (sec >= sec_first[st]) st++;
    if (over) w = SONG_M17;
    else if (g_phase == PH_LOGO) w = SND_NONE;
    else if (g_phase == PH_TITLE) w = SONG_M01;
    else if (g_phase == PH_SORTIE) w = SONG_M02;
    else if (g_calib && g_calib < 7) w = SONG_M10;               /* 7 面の試験 01..06 */
    else {
        w = sec_song[sec];
        if (w == A_) w = SONG_M03;
        else if (w == S_) w = stage_song[st];
        else if (w == MI) w = SONG_M12;
        else if (w == BO) w = SONG_M13;
        else if (w == OU) w = SONG_J01;
        else if (w == ST) w = SND_NONE;
    }
    if (g_pcm_cur == PCM_V04) v04 = 1;
    if (g_phase != ph_prev) { ph_prev = g_phase; if (g_phase == PH_LOGO) pcm_req(PCM_V03); }
    /* 試験の声: 1 つ目は測定空間から入るとき、2 つ目からは前の試験のボスを倒した直後(判定文の静かな間。次の試験の
       予告)。★試験の入りは読み込みと降下で重いフレームが続き、そこで鳴らすと遅れた */
    if (g_calib != calib_prev) { if (g_calib == 1) pcm_req(PCM_V06); calib_prev = g_calib; }
    if (g_boss_state == BS_DYING && bs_prev != BS_DYING) {
        if (g_calib && g_calib < 7) pcm_req(PCM_V06); else pcm_req_opt(PCM_P06);
        sfx(SFX_BOOM);
    }
    bs_prev = g_boss_state;
    if (g_phase >= PH_AIR && !over) {
        if (sec != sec_prev) { sec_prev = sec; mt = 0; on_sec(sec); }
        effects(sec);
        mt++;
    } else {
        sec_prev = 0xFF;
        g_snd_bend = 0; g_snd_detune = 0; g_snd_tempo = 128; g_snd_atten = 0; g_snd_mute = 0;
    }
    if (w == want) return;
    want = w;
    if (w == SONG_M03 && g_bgm == SONG_M02) return;              /* 出撃のファンファーレが続けて鳴らす */
    if (w == SONG_M13 && g_bgm == SONG_J02) return;
    snd_trans_next = (w == SONG_M03) ? air_trans[st] : 0;   /* 移調は次の曲の頭から(フェード中の曲の音程を変えない) */
    if (w == SONG_M13) { bgm_fade(SONG_J02, 1); pcm_req(PCM_V02); return; }   /* 警告 → ボス曲(短いフェード) */
    bgm_fade(w, g_bgm == SONG_M03 ? 4 : 1);         /* ★フェードアウトしてから: 空中戦 → 面の曲は約 1 秒、ほかは約 0.25 秒 */
}

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
void pcmx_play(u8 *p, u16 n); void pcmx_queue(u8 *p, u16 n); u8 pcmx_queued(void); u16 pcmx_left(void); void pcmx_stop(void);
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

static u16 pcm_fill(u8 half, u16 max) {
    u16 n = pcm_left_src > max ? max : pcm_left_src;
    rom_to_ram(pcm_bank, pcm_off, PCM_BUF + (half ? PCM_HALF : 0), n);
    pcm_off += n;
    pcm_left_src -= n;
    return n;
}

static void pcm_begin(u8 id) {
    g_pcm_cur = id;
    pcm_prio = snd_pcm_prio[id];
    pcm_bank = snd_pcm_bank[id];
    pcm_off = snd_pcm_off[id];
    pcm_left_src = snd_pcm_len[id];
    pcmx_play(PCM_BUF, pcm_fill(0, 128));  /* ★鳴り始めは少しだけ(場面の入りの重いフレームに重なりやすい)。2 面目は次のフレーム */
    pcm_last = 0;
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
        pcmx_queue(PCM_BUF + (h ? PCM_HALF : 0), pcm_fill(h, PCM_HALF));
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
