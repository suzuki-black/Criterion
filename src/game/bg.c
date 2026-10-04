/* bg.c — 1 面 空中戦の背景(指示書 5.1「夕焼けの雲海上空」)の絵と色。ラスターリストは stage.c が組む。
   ・空(リングの 0..99 行): 色 1 だけで塗り、ラスターで帯ごとに色 1 のパレットを変えてグラデーションにする(7 帯)。
   ・雲海(100..187 行): 遠・中・近の 3 層。各層の絵は 512 ドット周期で一度だけ描き、層ごとの横スクロールは
     ラスターでレジスタを書き換えるだけ。速さは 1/4・1/2・1。
   ・HUD 帯(188..211 行): ページ 2 の 212 行から(bg_fill_hud)。
   ・空と雲の色は bg_mix で夜(タイトル)と夕焼けの間を補間する。 */
#include "game.h"
#include "vdp.h"
#include "raster.h"
#include "spr.h"

#define SKY_BANDS 7

static u16 scroll_q;                /* 近景の位置(1/4 ドット単位) */
s8 g_scroll_speed = 4;              /* 1/4 ドット/フレーム単位(4 = 1 ドット/フレーム。負 = 逆走) */

static u8 regs_h[2][8];
u8 g_vscroll;                       /* プレイエリアの縦スクロール(R#23) */

/* 空(上から): 色 1 の R, G, B。夜(タイトル)と夕焼け(1 面)の間を bg_mix で補間する */
static const u8 sky_dusk[SKY_BANDS][3] = {
    { 1, 0, 3 }, { 2, 0, 4 }, { 3, 1, 4 }, { 4, 1, 4 }, { 5, 2, 4 }, { 6, 3, 3 }, { 7, 4, 2 } };
static const u8 sky_night[SKY_BANDS][3] = {
    { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 2 }, { 1, 0, 2 }, { 1, 1, 2 }, { 1, 1, 3 }, { 2, 1, 3 } };
/* 雲の色(パレット 2..7)。夜と夕焼け */
static const u8 cloud_dusk[6][3]  = { { 3, 1, 3 }, { 6, 3, 4 }, { 3, 1, 2 }, { 7, 4, 3 }, { 2, 1, 2 }, { 7, 6, 5 } };
static const u8 cloud_night[6][3] = { { 1, 1, 2 }, { 2, 2, 4 }, { 1, 1, 2 }, { 3, 3, 5 }, { 1, 0, 1 }, { 4, 4, 6 } };
static u8 sky_cur[SKY_BANDS][3];
static u8 mix_now = 0xFF;

/* 雲の層: 行の範囲、頂の高さ、色(暗 / 明) */
typedef struct { u8 y0, y1, base, amp, dark, light; } Layer;
static const Layer layer[3] = {
    { 100, 124, 10, 7, 2, 3 },     /* 遠 */
    { 124, 150, 10, 9, 4, 5 },     /* 中 */
    { 150, 188,  8, 8, 6, 7 },     /* 近 */
};

static u8 top_tab[512];            /* 作業用: 層ごとの雲の頂(行) */

static u8 lerp3(u8 a, u8 b, u8 t) { return (u8)(((u16)a * (16u - t) + (u16)b * t + 8u) >> 4); }

/* 空と雲の色: t = 0(夜)〜 16(夕焼け)。雲のパレットは全体(2..7)、空は帯ごと(ラスター) */
void bg_mix(u8 t) {
    u8 i, c;
    if (t > 16) t = 16;
    if (t == mix_now) return;
    mix_now = t;
    for (i = 0; i < SKY_BANDS; i++)
        for (c = 0; c < 3; c++) sky_cur[i][c] = lerp3(sky_night[i][c], sky_dusk[i][c], t);
    for (i = 0; i < 6; i++)
        vdp_set_pal((u8)(2 + i), lerp3(cloud_night[i][0], cloud_dusk[i][0], t),
                    lerp3(cloud_night[i][1], cloud_dusk[i][1], t), lerp3(cloud_night[i][2], cloud_dusk[i][2], t));
}

const u8 *bg_sky(u8 band) { return sky_cur[band]; }
u16 bg_layer_x(u8 l) { return l == 0 ? scroll_q >> 4 : l == 1 ? scroll_q >> 3 : scroll_q >> 2; }

/* HUD 帯の項目(188 行): ページ 2 の 212 行から、スクロール 0、属性表も HUD 用 */
void bg_fill_hud(RasEntry *e, u8 bi) {
    u8 *rh = regs_h[bi];
    rh[0] = 2; rh[1] = 0x7F;
    rh[2] = 23; rh[3] = HUD_R23;
    rh[4] = 5; rh[5] = spr_regs.r5h;
    rh[6] = 11; rh[7] = 3;
    e->line = HUD_Y; e->flags = RAS_SCROLL | RAS_REGS; e->r26 = 0; e->r27 = 0;
    e->haze_n = 4; e->haze = rh;
}

static void palette(void) {
    vdp_set_pal(0, 0, 0, 0);
    vdp_set_pal(1, 1, 0, 3);
    bg_mix(16);
    vdp_set_pal(8, 1, 1, 2);   vdp_set_pal(9, 2, 3, 5);   /* ★8 = スプライトの縁取り(暗い。2 枚重ねの下の層。M7 の後) */
    vdp_set_pal(10, 2, 6, 7);  vdp_set_pal(11, 6, 7, 7);
    vdp_set_pal(12, 6, 5, 2);  vdp_set_pal(13, 6, 3, 2);
    vdp_set_pal(14, 4, 4, 5);  vdp_set_pal(15, 7, 7, 7);
}

/* 層 l の雲の頂を 512 ドットぶん求める(周期 512 / 256 / 128 / 64 の正弦の和) */
static void make_tops(u8 l) {
    const Layer *L = &layer[l];
    u8 o2 = (u8)(l * 21), o3 = (u8)(l * 37), o4 = (u8)(l * 11), y0 = L->y0, amp = L->amp;
    s16 base = (s16)(L->y0 + L->base);
    u16 x;
    /* ★掛け算(ライブラリ呼び出し)を足し算とシフトにする(1 層 0.11 秒かかっていた)。amp は 7 / 8 / 9 */
    for (x = 0; x < 512; x++) {
        u8 xl = (u8)x;
        s16 s1 = g_sin64[(u8)(x >> 3) & 63], s2 = g_sin64[(u8)((u8)(x >> 2) + o2) & 63];
        s16 v = (s16)((s1 << 1) + s1 + (s2 << 1) + g_sin64[(u8)((u8)(x >> 1) + o3) & 63]
                      + (g_sin64[(u8)(xl + o4) & 63] >> 1));
        s16 a = (s16)(v << 3), t;
        if (amp == 7) a -= v; else if (amp == 9) a += v;          /* a = v * amp(|a| <= 7425) */
        t = (s16)(base + (a >> 10) + (a >> 12));                  /* ≒ a / 819 */
        if (t < (s16)y0) t = y0;
        top_tab[x] = (u8)t;
    }
}

/* 1 行を描く。画素の種類 = 頂からの距離 d = y - 頂(負 = 空 / 0..1 = 縁の光 / 2..3 = 光と影の市松 / 4.. = 影)。
   ★C で 1 画素ずつ求めると 1 バイト 54us かかり、起動と再開で 2 秒近く黒い画面が続いた。2 画素の種類の組
     → 1 バイトの表(行ごとに市松の位相で作る)を引く内側のループを asm にする。 */
u8 pair_tab[16];                   /* asm から参照するので static にしない */

/* 頂の表 t から 128 バイト(256 画素)を VRAM へ(アドレスは設定済み)。A = y、DE = t(sdcccall(1)) */
static void cloud_out(u8 y, const u8 *t) __naked {
    (void)y; (void)t;
    __asm
        ld   c, a                  ; C = y
        ex   de, hl                ; HL = t
        ld   b, #128
    00001$:
        ld   a, c
        sub  (hl)                  ; 偶数の画素の d
        inc  hl
        call 00010$
        add  a, a
        add  a, a
        ld   e, a
        ld   a, c
        sub  (hl)                  ; 奇数の画素の d
        inc  hl
        call 00010$
        or   e
        push hl
        ld   hl, #_pair_tab
        add  a, l
        ld   l, a
        adc  a, h
        sub  l
        ld   h, a
        ld   a, (hl)
        pop  hl
        out  (0x98), a
        djnz 00001$
        ret
        ; d(A)→ 種類: 負 = 0、0..1 = 1、2..3 = 2、4.. = 3
    00010$:
        bit  7, a
        jr   z, 00011$
        xor  a
        ret
    00011$:
        cp   #2
        jr   nc, 00012$
        ld   a, #1
        ret
    00012$:
        cp   #4
        ld   a, #2
        ret  c
        inc  a
        ret
    __endasm;
}

static void cloud_row(u8 l, u8 y) {
    const Layer *L = &layer[l];
    u8 i, p;
    for (i = 0; i < 16; i++) {
        u8 a = i >> 2, b = i & 3, ca, cb;
        ca = a == 0 ? 1 : a == 1 ? L->light : a == 2 ? ((y & 1) ? L->light : L->dark) : L->dark;     /* 偶数の x */
        cb = b == 0 ? 1 : b == 1 ? L->light : b == 2 ? ((y & 1) ? L->dark : L->light) : L->dark;     /* 奇数の x */
        pair_tab[i] = (u8)((ca << 4) | cb);
    }
    for (p = 0; p < 2; p++) {
        vdp_write_addr17(0, (u16)(p ? 0x8000u : 0u) + (u16)y * 128u);
        cloud_out(y, &top_tab[p ? 256 : 0]);
    }
}

void bg_init(void) {
    u8 l, y;
    palette();
    vdp_hmmv(0, 0, 256, 100, 0x11);                        /* 空(ページ 0) */
    vdp_hmmv(0, 256, 256, 100, 0x11);                      /* 空(ページ 1) */
    vdp_cmd_wait();
    for (l = 0; l < 3; l++) {
        make_tops(l);
        for (y = layer[l].y0; y < layer[l].y1; y++) cloud_row(l, y);
    }
    /* 188..191 行: 近い雲の底の続き(色 6)。都市の雲の底の上端と同じ色で、描き替えない(stage.c のブロック 23) */
    vdp_hmmv(0, 188, 256, 4, 0x66);
    vdp_hmmv(0, 256 + 188, 256, 4, 0x66);
    vdp_cmd_wait();
    scroll_q = 0;
}

void bg_scroll(void) {
    scroll_q += (u16)(s16)g_scroll_speed;
}
