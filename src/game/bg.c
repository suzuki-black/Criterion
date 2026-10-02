/* bg.c — 1 面 空中戦の背景(指示書 5.1「夕焼けの雲海上空」)とラスターリスト。
   ・空(0..99 行): 色 1 だけで塗り、ラスターで帯ごとに色 1 のパレットを変えてグラデーションにする(6 帯)。
   ・雲海(100..187 行): 遠・中・近の 3 層。各層の絵は 512 ドット周期で一度だけ描き、層ごとの横スクロールは
     ラスターでレジスタを書き換えるだけ(列の描き込み 0 = 毎フレームの VRAM 転送なし)。速さは 1/4・1/2・1。
   ・HUD 帯(188..211 行): スクロール 0 に戻す(ページ 0 の 0..255 ドットに描いた HUD を見せる)。
   ・96 行でスプライトの属性表を下の帯へ切り替える(spr.h)。 */
#include "game.h"
#include "vdp.h"
#include "raster.h"
#include "spr.h"

#define CLOUD_Y0  100
#define SKY_BANDS 7

static u16 scroll_q;                /* 近景の位置(1/4 ドット単位) */
u8 g_scroll_speed = 4;              /* 1/4 ドット/フレーム単位(4 = 1 ドット/フレーム) */

static u8 regs_a[2][8], regs_b[2][4], regs_h[2][8];
u8 g_vscroll;                       /* プレイエリアの縦スクロール(R#23) */

/* 夕焼けの空(上から): 色 1 の R, G, B */
static const u8 sky_rgb[SKY_BANDS][3] = {
    { 1, 0, 3 }, { 2, 0, 4 }, { 3, 1, 4 }, { 4, 1, 4 }, { 5, 2, 4 }, { 6, 3, 3 }, { 7, 4, 2 } };
static const u8 sky_line[SKY_BANDS] = { 0, 16, 32, 48, 64, 80, 96 };

/* 雲の層: 行の範囲、頂の高さ、色(暗 / 明) */
typedef struct { u8 y0, y1, base, amp, dark, light; } Layer;
static const Layer layer[3] = {
    { 100, 124, 10, 7, 2, 3 },     /* 遠 */
    { 124, 150, 10, 9, 4, 5 },     /* 中 */
    { 150, 188,  8, 8, 6, 7 },     /* 近 */
};

static u8 top_tab[512];            /* 作業用: 層ごとの雲の頂(行) */

static void palette(void) {
    vdp_set_pal(0, 0, 0, 0);
    vdp_set_pal(1, 1, 0, 3);
    vdp_set_pal(2, 3, 1, 3);   vdp_set_pal(3, 6, 3, 4);     /* 遠い雲 */
    vdp_set_pal(4, 3, 1, 2);   vdp_set_pal(5, 7, 4, 3);     /* 中の雲 */
    vdp_set_pal(6, 2, 1, 2);   vdp_set_pal(7, 7, 6, 5);     /* 近い雲 */
    vdp_set_pal(8, 7, 3, 1);   vdp_set_pal(9, 2, 3, 5);
    vdp_set_pal(10, 2, 6, 7);  vdp_set_pal(11, 6, 7, 7);
    vdp_set_pal(12, 6, 5, 2);  vdp_set_pal(13, 6, 3, 2);
    vdp_set_pal(14, 4, 4, 5);  vdp_set_pal(15, 7, 7, 7);
}

/* 層 l の雲の頂を 512 ドットぶん求める(周期 512 / 256 / 128 / 64 の正弦の和) */
static void make_tops(u8 l) {
    const Layer *L = &layer[l];
    u16 x;
    for (x = 0; x < 512; x++) {
        s16 v = (s16)g_sin64[(x >> 3) & 63] * 3
              + (s16)g_sin64[((x >> 2) + l * 21) & 63] * 2
              + (s16)g_sin64[((x >> 1) + l * 37) & 63]
              + (s16)(g_sin64[(x + l * 11) & 63] >> 1);
        s16 t = (s16)L->y0 + L->base + (s16)(v * (s16)L->amp / 860);   /* |v| <= 825、v * amp は 16bit に収まる */
        if (t < (s16)L->y0) t = L->y0;
        top_tab[x] = (u8)t;
    }
}

static u8 cloud_pixel(u8 l, u16 x, u8 y) {
    const Layer *L = &layer[l];
    u8 t = top_tab[x];
    if (y < t) return 1;                                   /* 空 */
    if (y < t + 2) return L->light;                        /* 縁の光(夕日) */
    if (y < t + 4) return ((x + y) & 1) ? L->light : L->dark;
    return L->dark;
}

void bg_init(void) {
    u8 l, y, p;
    palette();
    vdp_hmmv(0, 0, 256, 100, 0x11);                        /* 空(ページ 0) */
    vdp_hmmv(0, 256, 256, 100, 0x11);                      /* 空(ページ 1) */
    vdp_cmd_wait();
    for (l = 0; l < 3; l++) {
        make_tops(l);
        for (y = layer[l].y0; y < layer[l].y1; y++)
            for (p = 0; p < 2; p++) {
                u16 x, x0 = (u16)p * 256u;
                vdp_write_addr17(0, (u16)(p ? 0x8000u : 0u) + (u16)y * 128u);
                for (x = x0; x < x0 + 256u; x += 2)
                    VDP_DAT = (u8)((cloud_pixel(l, x, y) << 4) | cloud_pixel(l, x + 1, y));
            }
    }
    scroll_q = 0;
}

void bg_scroll(void) {
    scroll_q += g_scroll_speed;
}

void bg_build(void) {
    RasEntry *e = ras_back();
    u8 bi = ras_back_index();
    u8 *ra = regs_a[bi], *rb = regs_b[bi], *rh = regs_h[bi];
    u8 i, k = 0;
    u16 near = scroll_q >> 2, mid = scroll_q >> 3, far = scroll_q >> 4;

    ra[0] = 5; ra[1] = spr_regs.r5a; ra[2] = 11; ra[3] = spr_regs.r11a;
    ra[4] = 2; ra[5] = 0x3F;                                /* プレイエリア: 表示リング(ページ 0|1) */
    ra[6] = 23; ra[7] = g_vscroll;
    rb[0] = 5; rb[1] = spr_regs.r5b; rb[2] = 11; rb[3] = spr_regs.r11b;
    rh[0] = 2; rh[1] = 0x7F;                                /* HUD 帯: ページ 2 の 212 行から */
    rh[2] = 23; rh[3] = HUD_R23;
    rh[4] = 5; rh[5] = spr_regs.r5h;                        /* HUD 帯は属性表も別 */
    rh[6] = 11; rh[7] = 3;

    for (i = 0; i < SKY_BANDS; i++, k++) {
        e[k].line = sky_line[i];
        e[k].flags = RAS_PAL;
        e[k].pidx = 1;
        e[k].prb = (u8)((sky_rgb[i][0] << 4) | sky_rgb[i][2]);
        e[k].pg = sky_rgb[i][1];
    }
    e[0].flags = RAS_PAL | RAS_SCROLL | RAS_REGS;          /* 先頭: 空はどこでも同じ色。上の帯の属性表 */
    e[0].r26 = 0; e[0].r27 = 0;
    e[0].haze_n = 4; e[0].haze = ra;
    e[SKY_BANDS - 1].flags = RAS_PAL | RAS_REGS;            /* 96 行: 下の帯の属性表へ */
    e[SKY_BANDS - 1].haze_n = 2; e[SKY_BANDS - 1].haze = rb;

    e[k].line = CLOUD_Y0; e[k].flags = RAS_SCROLL; e[k].r26 = VDP_R26(far);  e[k].r27 = VDP_R27(far);  k++;
    e[k].line = 124;      e[k].flags = RAS_SCROLL; e[k].r26 = VDP_R26(mid);  e[k].r27 = VDP_R27(mid);  k++;
    e[k].line = 150;      e[k].flags = RAS_SCROLL; e[k].r26 = VDP_R26(near); e[k].r27 = VDP_R27(near); k++;
    e[k].line = HUD_Y;    e[k].flags = RAS_SCROLL | RAS_REGS; e[k].r26 = 0; e[k].r27 = 0;
    e[k].haze_n = 4; e[k].haze = rh; k++;
    ras_commit(k);
}
