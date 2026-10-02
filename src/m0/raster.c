/* raster.c(M0) — M0 技術検証 (2): 多段ラスタースクロール＋HR ポーリング帯。指示書 8.4 / 9(M0-2)。

   画面:
     0..15     表示帯(スクロールしない。状態と CPU 残量を表示)
     16..163   N 本の帯(2〜16)。帯ごとに別速度・別方向で横スクロールし、目印(パレット 15)の色を変える
     164..211  陽炎帯(48 ライン)。HR ポーリングで毎ライン R#26/R#27 を正弦波で変える(指示書 5.4)
   割込みの入口は BIOS 方式(H.KEYI)と直接方式(ページ 0 RAM の 0x0038)を切り替えられる。
   ISR の間は枠線をパレット 13(黄)にする＝走査線のどこで割込みが走っているかが枠に出る。

   操作(実機用): ←→ 帯の本数 / SPACE 陽炎帯 切/R#26+27/R#27のみ / ↑ 直接方式 / ↓ BIOS 方式 / INS・DEL 補正(bias)±1
   デバッガ用: g_cfg を書くと同じ切替ができる(freeze=1 で時間を止める)。g_m2 に計測値。 */
#include "types.h"
#include "vdp.h"
#include "sys.h"
#include "frame.h"
#include "raster.h"
#include "font.h"

#define TOP      16
#define AREA     148
#define HAZE_Y   164
#define HAZE_N   48
#define BANDS_MIN 2
#define BANDS_MAX 16

typedef struct {
    u8 bands;      /* 帯の本数 */
    u8 haze;       /* 陽炎帯 on/off */
    u8 mode;       /* RAS_ISR_BIOS / RAS_ISR_DIRECT */
    u8 freeze;     /* 1 = 時間を止める(画素照合用) */
} M2Cfg;

typedef struct {
    u16 base[2];   /* 分割なしのアイドル回数(方式別) */
    u16 idle_min;  /* 設定を変えてからの最小アイドル回数 */
    u16 idle_last;
    u16 late;      /* 設定を変えてから、表示の描き直し以外で遅れたフレーム数 */
    u16 t;         /* 時刻(フレーム。freeze 中は止まる) */
    u8  mode;      /* 実際の割込み方式 */
    u8  n;         /* 表の項目数 */
    u8  ramx;      /* 1 = RAM 実行中 */
    u16 frames;    /* 設定を変えてから数えたフレーム数(表示の描き直しを除く) */
    u32 idle_sum;  /* その間のアイドル回数の合計(平均 = idle_sum / frames) */
} M2Stat;

volatile M2Cfg  g_cfg = { 12, 1, RAS_ISR_BIOS, 0 };
volatile M2Stat g_m2;

static const s8 sine[64] = {
    0, 1, 2, 3, 5, 6, 7, 8, 8, 9, 10, 11, 11, 11, 12, 12, 12, 12, 12, 11, 11, 11, 10, 9, 8, 8, 7, 6, 5, 3, 2, 1,
    0, -1, -2, -3, -5, -6, -7, -8, -8, -9, -10, -11, -11, -11, -12, -12, -12, -12, -12, -11, -11, -11, -10, -9,
    -8, -8, -7, -6, -5, -3, -2, -1 };

/* 帯の目印色(R, G, B)。黒・格子・目盛り・白(表示帯)・水色(陽炎)・黄(ISR)と重ならない 16 色。 */
static const u8 band_rgb[BANDS_MAX][3] = {
    {7,0,0},{0,7,0},{0,0,7},{7,4,0},{4,0,7},{0,4,7},{7,0,7},{4,7,0},
    {7,4,4},{4,4,7},{4,7,4},{7,2,5},{2,5,7},{5,7,2},{6,3,0},{0,3,6} };

static u8 haze_tab[2][HAZE_N * 2];

/* ---- 横スクロール位置 → レジスタ(M0-1 で検証済みの換算) ---- */
#define R26(x) ((u8)((((x) + 7u) >> 3) & 63u))
#define R27(x) ((u8)((0u - (x)) & 7u))

static s16 band_speed(u8 b) {
    s16 v = (s16)(1 + (b & 3));
    return (b & 1) ? (s16)-v : v;
}

static u8 band_line(u8 b, u8 n) {
    return (u8)(TOP + (u16)b * AREA / n);
}

/* ---- 背景: ページ 0-1(512 幅)の 16..211 行に目印・目盛り・格子 ---- */
static u8 pat(u16 x, u8 y) {
    if ((x & 31) < 4) return 15;
    if ((x & 7) == 0) return 2;
    if ((y & 15) == 0) return 1;
    return 0;
}

static void draw_bg(void) {
    u8 y, p;
    u16 x;
    for (y = TOP; y < 212; y++)
        for (p = 0; p < 2; p++) {
            vdp_write_addr17(0, (u16)(p ? 0x8000u : 0u) + (u16)y * 128u);
            for (x = (u16)p * 256u; x < (u16)p * 256u + 256u; x += 2)
                VDP_DAT = (u8)((pat(x, y) << 4) | pat(x + 1, y));
        }
}

static void palette(void) {
    vdp_set_pal(0, 0, 0, 0);
    vdp_set_pal(1, 1, 1, 2);    /* 格子 */
    vdp_set_pal(2, 2, 2, 2);    /* 目盛り */
    vdp_set_pal(13, 7, 7, 0);   /* ISR 中の枠線 */
    vdp_set_pal(15, 7, 7, 7);
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
    vdp_wreg(26, 0);
    vdp_wreg(27, 0);
    vdp_wreg(7, 0);
    palette();
}

/* ---- 表を組む ---- */
static u8 build(u16 t) {
    RasEntry *e = ras_back();
    u8 *hz = haze_tab[ras_back_index()];
    u8 n = g_cfg.bands, b, k = 0;
    if (n < BANDS_MIN) n = BANDS_MIN;
    if (n > BANDS_MAX) n = BANDS_MAX;

    /* 先頭: 表示帯(スクロール 0、目印=白) */
    e[k].line = 0; e[k].flags = RAS_SCROLL | RAS_PAL;
    e[k].r26 = 0; e[k].r27 = 0;
    e[k].pidx = 15; e[k].prb = 0x77; e[k].pg = 7;
    k++;
    for (b = 0; b < n; b++, k++) {
        u16 x = (u16)((s16)t * band_speed(b));
        e[k].line = band_line(b, n);
        e[k].flags = RAS_SCROLL | RAS_PAL;
        e[k].r26 = R26(x); e[k].r27 = R27(x);
        e[k].pidx = 15;
        e[k].prb = (u8)((band_rgb[b][0] << 4) | band_rgb[b][2]);
        e[k].pg = band_rgb[b][1];
    }
    if (g_cfg.haze) {
        /* haze=1: R#26+R#27 を毎ライン(振幅 ±12) / haze=2: R#27 だけ(振幅 ±3、帰線での書き込み 1 回) */
        u8 i, fine = g_cfg.haze == 2;
        for (i = 0; i < HAZE_N; i++) {
            s8 w = sine[(u8)(i * 4 + t * 2) & 63];
            u16 x = fine ? (u16)((t & ~7u) + 4 + w / 4) : (u16)(t + w);
            hz[i * 2] = R26(x);
            hz[i * 2 + 1] = R27(x);
        }
        e[k].line = HAZE_Y;
        e[k].flags = (u8)(RAS_SCROLL | RAS_PAL | RAS_HAZE | (fine ? RAS_HAZE_FINE : 0));
        e[k].r26 = hz[0]; e[k].r27 = hz[1];
        e[k].pidx = 15; e[k].prb = 0x07; e[k].pg = 7;   /* 水色 */
        e[k].haze_n = HAZE_N;
        e[k].haze = hz;
        k++;
    }
    ras_commit(k);
    return k;
}

/* ---- キー(PPI 直読み。BIOS を使わない) ---- */
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

static void hud_static(void) {
    font_text(0, 8, 0, 15, 0, "M0-2 RASTER");
}

static void hud(void) {
    u16 base = g_m2.base[g_m2.mode];
    u16 avg = g_m2.frames ? (u16)(g_m2.idle_sum / g_m2.frames) : 0;
    u16 pct = base ? (u16)((u32)avg * 100u / base) : 0;   /* CPU 残量(平均) */
    font_text(0, 104, 0, 15, 0, g_m2.mode == RAS_ISR_DIRECT ? "DIRECT" : "BIOS  ");
    font_text(0, 160, 0, 15, 0, "B");
    font_num(0, 168, 0, 15, 0, g_cfg.bands, 2);
    font_text(0, 192, 0, 15, 0, g_cfg.haze == 2 ? "HZ27" : g_cfg.haze ? "HAZE" : "    ");
    font_text(0, 232, 0, 15, 0, g_m2.ramx ? "R" : " ");
    font_text(0, 8, 8, 15, 0, "IDLE");
    font_num(0, 48, 8, 15, 0, pct, 3);
    font_text(0, 80, 8, 15, 0, "LATE");
    font_num(0, 120, 8, 15, 0, g_m2.late, 5);
    font_text(0, 176, 8, 15, 0, "BIAS");
    font_num(0, 216, 8, 15, 0, g_ras_bias, 1);
}

static u16 measure_base(void) {
    u8 i;
    u16 m = 0;
    frame_sync(1);
    for (i = 0; i < 8; i++) { u16 v = frame_sync(1); if (v > m) m = v; }
    return m;
}

void main(void) {
    u16 t = 0;
    u8 prev_keys = 0, hud_tick = 0, skip_min = 1;
    M2Cfg last;

    sys_r800();
    screen_init();
#ifndef NO_RAMX
    g_m2.ramx = sys_ram_exec();      /* 以後 ROM は見えない(この ROM はバンクを使わない) */
#endif
    vdp_hmmv(0, 0, 256, 512, 0x00);
    vdp_cmd_wait();                  /* ★消去が終わる前に CPU で描くと、追いついた消去に先頭行を消される */
    draw_bg();
    hud_static();
    ras_init();
    frame_begin();

    /* 分割なしの基準(方式別) */
    g_m2.base[RAS_ISR_BIOS] = measure_base();
    g_m2.mode = ras_set_isr(RAS_ISR_DIRECT);
    if (g_m2.mode == RAS_ISR_DIRECT) {
        g_m2.base[RAS_ISR_DIRECT] = measure_base();
        g_m2.mode = ras_set_isr(RAS_ISR_BIOS);
    }
    g_ras_busy_border = 13;
    last = g_cfg;
    g_m2.idle_min = 0xFFFF;

    for (;;) {
        u8 keys = key_row(8);
        u8 edge = (u8)(keys & ~prev_keys);
        u16 idle;
        prev_keys = keys;
        if ((edge & 0x80) && g_cfg.bands < BANDS_MAX) g_cfg.bands++;   /* → */
        if ((edge & 0x10) && g_cfg.bands > BANDS_MIN) g_cfg.bands--;   /* ← */
        if (edge & 0x01) g_cfg.haze = (u8)((g_cfg.haze + 1) % 3);      /* SPACE */
        if (edge & 0x20) g_cfg.mode = RAS_ISR_DIRECT;                  /* ↑ */
        if (edge & 0x40) g_cfg.mode = RAS_ISR_BIOS;                    /* ↓ */
        if ((edge & 0x04) && g_ras_bias < 9) g_ras_bias++;              /* INS */
        if ((edge & 0x08) && g_ras_bias > 0) g_ras_bias--;              /* DEL */

        if (g_cfg.mode != g_m2.mode) g_m2.mode = ras_set_isr(g_cfg.mode);
        if (g_cfg.bands != last.bands || g_cfg.haze != last.haze || g_cfg.mode != last.mode) {
            last = g_cfg;
            g_m2.idle_min = 0xFFFF;
            g_m2.late = 0;
            g_m2.frames = 0;
            g_m2.idle_sum = 0;
            skip_min = 2;
        }

        if (!g_cfg.freeze) t++;
        g_m2.t = t;
        g_m2.n = build(t);

        if (++hud_tick >= 30) { hud_tick = 0; hud(); skip_min = 2; }   /* 描き直したフレームと次のフレームは除く */

        {
            u16 late0 = g_frame_late;
            idle = frame_sync(1);
            g_m2.idle_last = idle;
            if (skip_min) skip_min--;      /* 表示の描き直し・設定変更のフレームは除く */
            else {
                if (g_frame_late != late0) g_m2.late++;
                if (idle < g_m2.idle_min) g_m2.idle_min = idle;
                g_m2.frames++;
                g_m2.idle_sum += idle;
            }
        }
    }
}
