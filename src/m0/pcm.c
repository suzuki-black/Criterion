/* pcm.c(M0) — M0 技術検証 (3): PCM 方式 A/B/C の比較。指示書 8.6 / 9(M0-3)。

   M0-1 のスクロール(速度プログラム: 0.5→8 ドット/フレーム、急停止、逆走。列の描き込み)と
   M0-2 の 12 帯ラスター(帯ごとに視差のずれ。陽炎帯 R#27 は切替)を動かしながら、試験音
   (tools/gen_pcm.py のオーケストラヒット風の和音)を途切れなくループ再生する。
     方式 A … 走査線割込み 2 ラインごとに 1 サンプル。割込みが起きない区間は出さない(raster.c)
     方式 B … S1990 タイマを待ちループで見て出す(pcmb.c)
     方式 C … A + 割込みが起きない区間(246..261 行、HR ポーリング帯)も HR を数えて出す(raster.c)
   割込みは直接方式(M0-2 で標準に決めた)。

   操作(実機用): 0 = PCM なし / 1 = A / 2 = B / 3 = C / SPACE = 陽炎帯 on/off
   デバッガ用: g_cfg.method / g_cfg.haze を書くと同じ切替。g_m3 に計測値。
   出力の時刻は openMSX のウォッチポイント(D/A ポート 0xA4 への書き込み)で測る(tools/test/run_m0_pcm.sh)。 */
#include "types.h"
#include "vdp.h"
#include "sys.h"
#include "frame.h"
#include "raster.h"
#include "pcmb.h"
#include "world.h"
#include "pcm_data.h"

#define BANDS     12
#define TOP       16
#define BAND_H    12
#define HAZE_Y    164
#define HAZE_N    48
#define PARALLAX  24        /* 帯ごとの横ずれの最大(ドット) */
#define AHEAD     (264 + PARALLAX + 8)
#define BEHIND    (8 + PARALLAX + 8)

typedef struct {
    u8 method;     /* 0 = なし / 1 = A / 2 = B / 3 = C */
    u8 haze;
} M3Cfg;

typedef struct {
    u16 base;      /* 分割も PCM もなしのアイドル回数 */
    u16 idle_min;
    u16 late;      /* 設定を変えてから遅れたフレーム数 */
    u16 frames;
    u32 idle_sum;
    u16 plays;     /* 試験音を頭から鳴らした回数 */
    u8  method;    /* 実際に動いている方式 */
} M3Stat;

volatile M3Cfg  g_cfg = { 3, 1 };
volatile M3Stat g_m3;

static u8 haze_tab[2][HAZE_N * 2];
static const s8 sine[16] = { 0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1 };

__sfr __at(0xA5) PCM_CTRL;

/* ---- 表を組む: 先頭(スクロール x)+ 12 帯(視差のずれ)+ 陽炎帯(R#27 のみ) ---- */
static void build(u16 x, u16 t) {
    RasEntry *e = ras_back();
    u8 *hz = haze_tab[ras_back_index()];
    u8 b, k = 0;
    e[k].line = 0; e[k].flags = RAS_SCROLL;
    e[k].r26 = R26(x); e[k].r27 = R27(x);
    k++;
    for (b = 0; b < BANDS; b++, k++) {
        s16 off = (s16)((s16)(sine[(u8)(b * 3 + (t >> 2)) & 15]) * (PARALLAX / 3));
        u16 bx = (u16)(x + off);
        e[k].line = (u8)(TOP + b * BAND_H);
        e[k].flags = RAS_SCROLL;
        e[k].r26 = R26(bx); e[k].r27 = R27(bx);
    }
    if (g_cfg.haze) {
        u8 i;
        for (i = 0; i < HAZE_N; i++) {
            u16 hx = (u16)((x & ~7u) + 4 + sine[(u8)(i + t) & 15]);
            hz[i * 2] = R26(hx);
            hz[i * 2 + 1] = R27(hx);
        }
        e[k].line = HAZE_Y;
        e[k].flags = RAS_SCROLL | RAS_HAZE | RAS_HAZE_FINE;
        e[k].r26 = hz[0]; e[k].r27 = hz[1];
        e[k].haze_n = HAZE_N;
        e[k].haze = hz;
        k++;
    }
    ras_commit(k);
}

static void set_method(u8 m) {
    pcm_stop();
    pcmb_stop();
    if (m == 1) pcm_play(pcm_hit, PCM_HIT_LEN, 0);
    if (m == 2) pcmb_play(pcm_hit, PCM_HIT_LEN);
    if (m == 3) pcm_play(pcm_hit, PCM_HIT_LEN, 1);
    g_m3.method = m;
}

/* 途切れなく続けるため、次の 1 周を常に予約しておく(割込み側が切れ目なくつなぐ) */
static void keep_playing(void) {
    u8 m = g_m3.method;
    if (m == 1 || m == 3) {
        if (!pcm_queued()) { pcm_queue(pcm_hit, PCM_HIT_LEN); g_m3.plays++; }
    } else if (m == 2) {
        if (!pcmb_queued()) { pcmb_queue(pcm_hit, PCM_HIT_LEN); g_m3.plays++; }
    }
}

__sfr __at(0xA9) PPI_B;
__sfr __at(0xAA) PPI_C;

static u8 key_row(u8 row) {
    u8 v;
    __asm di __endasm;
    PPI_C = (u8)((PPI_C & 0xF0) | row);
    v = PPI_B;
    __asm ei __endasm;
    return (u8)~v;
}

static void screen_init(void) {
    __asm
        ld   a, #5
        ld   (0xFCAF), a
        call 0x005F          ; CHGMOD(BIOS を使うのは初期化だけ)
    __endasm;
    vdp_wreg(8, 0x2A);       /* TP=1, VR=1, SPD=1 */
    vdp_wreg(9, 0x80);
    vdp_wreg(2, 0x3F);       /* SP2: ページ 0|1 */
    vdp_wreg(25, 0x03);      /* SP2 | MSK */
    vdp_wreg(7, 0);
    world_palette();
}

void main(void) {
    WorldRun run = { 0, 0, 0, 0, 0 };
    u16 t = 0;
    u8 i, prev0 = 0, prev8 = 0, skip = 2;
    M3Cfg last;

    sys_r800();
    screen_init();
    vdp_hmmv(0, 0, 256, 512, 0x00);
    vdp_cmd_wait();
    world_gen_strips();
    world_reset(0);
    for (i = 0; i < 24; i++) world_stream(0, AHEAD, BEHIND);
    vdp_cmd_wait();
    sys_ram_exec();
    /* D/A: MUTE を解除(bit1 = 1)、D/A 出力・サンプル(bit0, 3, 4 = 0)。フィルタ(bit2)は今の値のまま。
       ★ビットの意味は openMSX の実装に合わせた。実機資料で確認すること(指示書 8.6)。 */
    PCM_CTRL = (u8)((PCM_CTRL & 0x04) | 0x02);
    ras_init();
    ras_set_isr(RAS_ISR_DIRECT);
    g_ras_busy_border = 0;
    frame_begin();
    { u8 j; u16 m = 0; frame_sync(1); for (j = 0; j < 8; j++) { u16 v = frame_sync(1); if (v > m) m = v; } g_m3.base = m; }

    last.method = 0xFF;
    for (;;) {
        u8 k0 = key_row(0), k8 = key_row(8);
        u8 e0 = (u8)(k0 & ~prev0), e8 = (u8)(k8 & ~prev8);
        u16 idle;
        prev0 = k0; prev8 = k8;
        for (i = 0; i < 4; i++) if (e0 & (1 << i)) g_cfg.method = i;   /* 0..3 */
        if (e8 & 0x01) g_cfg.haze ^= 1;                                 /* SPACE */

        if (g_cfg.method != last.method || g_cfg.haze != last.haze) {
            if (g_cfg.method != last.method) set_method(g_cfg.method);
            last = g_cfg;
            g_m3.idle_min = 0xFFFF;
            g_m3.late = 0;
            g_m3.frames = 0;
            g_m3.idle_sum = 0;
            skip = 2;
        }
        keep_playing();

        world_speed_step(&run);
        t++;
        world_stream(run.posi, AHEAD, BEHIND);
        build(run.posi, t);
        vdp_cmd_wait();

        {
            u16 late0 = g_frame_late;
            idle = frame_sync(1);
            if (skip) skip--;
            else {
                if (g_frame_late != late0) g_m3.late++;
                if (idle < g_m3.idle_min) g_m3.idle_min = idle;
                g_m3.frames++;
                g_m3.idle_sum += idle;
            }
        }
    }
}
