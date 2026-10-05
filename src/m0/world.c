/* world.c — M0 の検証 ROM が共有する「世界」: 地形の列ストリップ、列ストリーミング、速度プログラム。
   M0-1(scroll.c)で 1 ドット単位に検証した実装を、M0-3 でも同じ負荷として使うために切り出したもの。 */
#include "world.h"
#include "vdp.h"
#include "font.h"
#include "sys.h"

#define RING_W     512u

/* 置き場所(SCREEN5 のページ番号)。既定: リング = ページ 0|1、ストリップ = ページ 2。
   M0-4 では SCREEN12 の帯がページ 0 の前半を使うので、リング = 2|3、ストリップ = 1 にする。 */
static u8 ring_page = 0, strip_page = 2;
static u8 row0 = 0;   /* この行より上は描かない(M0-4: 上の SCREEN12 の帯を壊さないため) */

void world_set_pages(u8 ring, u8 strip) { ring_page = ring; strip_page = strip; }
void world_set_top(u8 y0) { row0 = y0; }

u16 g_world_cols;

/* ---- 世界(地形)の定義: 列番号 c(8 ドット単位)→ ストリップ番号 0..31 ---- */
u8 world_strip_of(u16 c) {
    u16 blk = c >> 2;                               /* 4 列(32 ドット)ごとに地形が変わる */
    u8 ceil  = (u8)((blk * 5u + (blk >> 3)) & 3);
    u8 floor = (u8)((blk * 3u + (blk >> 2) + 1u) & 3);
    u8 major = (c & 7) == 0;                        /* 64 ドットごとに大目盛り */
    return (u8)((major << 4) | (floor << 2) | ceil);
}

/* ストリップ s の (x,y) の色。ceil/floor は 1..4 段(8 ドット/段)。 */
static u8 strip_pixel(u8 s, u8 x, u8 y) {
    u8 ceil_h  = (u8)(((s & 3) + 1) * 8);
    u8 floor_h = (u8)((((s >> 2) & 3) + 1) * 8);
    u8 major   = s >> 4;
    u8 top     = 13;
    u8 fy      = (u8)(212 - floor_h);
    if (y < 12) {                                   /* 定規(目盛り) */
        if (x == 0 && (major || y >= 6)) return 15;
        return 1;
    }
    if (y == 12) return 14;
    if (y < top + ceil_h) {                         /* 天井ブロック */
        u8 ry = (u8)(y - top);
        if ((ry & 7) == 7) return 5;
        if (x == (((ry >> 3) & 1) ? 3 : 7)) return 5;
        return 4;
    }
    if (y >= fy) {                                  /* 床ブロック */
        u8 ry = (u8)(y - fy);
        if (ry == 0) return 10;
        if ((ry & 7) == 7) return 7;
        if (x == (((ry >> 3) & 1) ? 3 : 7)) return 7;
        return 6;
    }
    if (major && x == 0 && (y & 4)) return 11;      /* 大目盛りの破線 */
    if (x == 3 && y == (u8)(40 + s * 5)) return 2;  /* 星 */
    return 0;
}

void world_gen_strips(void) {
    u8 y, s, x;
    for (y = 0; y < 212; y++) {
        vdp_write_addr17((u8)(strip_page >> 1), (u16)((strip_page & 1) ? 0x8000u : 0u) + (u16)y * 128u);
        for (s = 0; s < 32; s++)
            for (x = 0; x < 8; x += 2)
                VDP_DAT = (u8)((strip_pixel(s, x, y) << 4) | strip_pixel(s, (u8)(x + 1), y));
    }
}

/* ---- 列ストリーミング: 描き込み済み範囲 [lo, hi)(世界座標、8 の倍数、16bit 巡回) ---- */
static u16 lo, hi;

void world_reset(u16 x) { lo = hi = x & ~7u; }

static void draw_col(u16 wx) {
    u16 rx = wx & (RING_W - 1);
    u8  s  = world_strip_of(wx >> 3);
    vdp_hmmm((u16)s * 8u, (u16)strip_page * 256u + row0, rx & 255u,
             (u16)ring_page * 256u + ((rx & 256u) ? 256u : 0u) + row0, 8, (u16)(212u - row0));
    g_world_cols++;
}

/* 表示位置 x に対し、右に ahead・左に behind ドットの描き込み済みを保つ(1 回の列数に上限あり)。 */
void world_stream(u16 x, s16 ahead, s16 behind) {
    u8 n = 0;
    /* 描き込み済み範囲の外へ飛んだ(ワープ)ら、間を順に描かずにリングを張り直す */
    if ((s16)(x - lo) < 0 || (s16)(x - hi) >= 0) lo = hi = x & ~7u;
    while ((s16)(hi - x) < ahead && n < 4) {
        if ((u16)(hi - lo) >= RING_W) lo += 8;
        draw_col(hi);
        hi += 8;
        n++;
    }
    while ((s16)(x - lo) < behind && n < 8) {
        if ((u16)(hi - lo) >= RING_W) hi -= 8;
        lo -= 8;
        draw_col(lo);
        n++;
    }
}

/* ---- 速度プログラム ---- */
static const Seg prog[] = {
    {  128,   4,  90 },   /* 0.5 */
    {  256,   8,  90 },   /* 1   */
    {  512,  16,  90 },   /* 2   */
    { 1024,  32,  90 },   /* 4   */
    { 2048,  64, 150 },   /* 8   */
    {    0,  69,  60 },   /* 急停止(約 1/2 秒) */
    {-2048,  32, 150 },   /* 逆走 8 */
    {    0,  69,  60 },   /* 停止 */
};
#define NSEG (sizeof(prog) / sizeof(prog[0]))

/* 速度プログラムを 1 フレーム進める。 */
void world_speed_step(WorldRun *r) {
    const Seg *sg = &prog[r->seg];
    if (r->speed < sg->target) { r->speed += sg->accel; if (r->speed > sg->target) r->speed = sg->target; }
    else if (r->speed > sg->target) { r->speed -= sg->accel; if (r->speed < sg->target) r->speed = sg->target; }
    else if (++r->hold >= sg->hold) { r->hold = 0; if (++r->seg >= NSEG) r->seg = 0; }
    {
        s16 acc = (s16)r->posf + r->speed;
        r->posi += (u16)(acc >> 8);
        r->posf = (u8)acc;
    }
}


void world_palette(void) {
    vdp_set_pal(0, 0, 0, 0);
    vdp_set_pal(1, 1, 1, 2);    /* 定規の地 */
    vdp_set_pal(2, 4, 4, 5);    /* 星 */
    vdp_set_pal(4, 4, 2, 1);    /* 天井ブロック */
    vdp_set_pal(5, 2, 1, 0);    /* 天井目地 */
    vdp_set_pal(6, 2, 3, 5);    /* 床ブロック */
    vdp_set_pal(7, 1, 1, 3);    /* 床目地 */
    vdp_set_pal(10, 5, 6, 7);   /* 床の縁 */
    vdp_set_pal(11, 7, 1, 1);   /* 大目盛り破線 */
    vdp_set_pal(14, 5, 5, 5);   /* 定規の線 */
    vdp_set_pal(15, 7, 7, 7);   /* 目盛り */
}


/* ---- M0 の検証 ROM だけが使うもの(本体の常駐の空きのため core から移した。M7 の後) ---- */
/* 10 進 n 桁(先頭ゼロ詰め)。 */
void font_num(u8 page, u8 px, u8 py, u8 fg, u8 bg, u16 v, u8 digits) {
    char buf[6];
    u8 i;
    if (digits > 5) digits = 5;
    buf[digits] = 0;
    for (i = digits; i > 0; i--) { buf[i - 1] = (char)('0' + v % 10u); v /= 10u; }
    font_text(page, px, py, fg, bg, buf);
}

void sys_hook_vblank(void (*fn)(void)) {
    volatile u8 *h = (volatile u8 *)0xFD9F;   /* H.TIMI */
    __asm di __endasm;
    h[1] = (u8)(u16)fn;
    h[2] = (u8)((u16)fn >> 8);
    h[0] = 0xC3;                               /* JP fn */
    __asm ei __endasm;
}
