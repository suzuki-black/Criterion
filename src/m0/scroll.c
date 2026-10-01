/* scroll.c — M0 技術検証 (1): SP2 リングでの 0〜8 ドット/フレーム可変速・急停止・逆走。
   指示書 8.5 / 9(M0-1)。

   構成:
     - SCREEN5 + R#25 SP2|MSK。ページ 0-1 を 512x256 の横リングとして表示し、R#26/R#27 で位置を決める。
     - ページ 2 に 8x212 の「列ストリップ」を 32 本プリ生成し、世界の列ごとに 1 回の HMMM で描き込む
       (指示書 8.5「列単位の HMMM 転送で済むようにする」)。
     - スクロールレジスタは VBLANK(H.TIMI)で適用し、メインは次フレームの位置と列の描き込みを準備する。
     - 速度プログラム: 0.5→1→2→4→8 ドット/フレームへ加速、1/2 秒で急停止、逆走 8 ドット/フレーム、停止、の繰り返し。

   計測(openMSX/実機のデバッガで g_m0 を読む):
     idle_base  = 何もしないフレームのアイドルループ回数(基準)
     idle_min   = 走行中の最小アイドル回数(= CPU 残量の最悪値。残量% = idle_min/idle_base)
     miss       = 1 フレームに収まらず、待たずに次のフレームへ進んだ回数(frame_sync の遅れ)
     cols       = 描き込んだ列の総数
   枠線色(R#7): 列の描き込み中は 4、待機中は 0(指示書 8.13 の負荷可視化)。 */
#include "types.h"
#include "vdp.h"
#include "sys.h"
#include "frame.h"

#define PAGE2_Y    512u     /* ページ 2 のコマンド座標 Y */
#define RING_W     512u
#define AHEAD      264      /* 右側に確保しておく描き込み済み幅(表示幅 + 1 列) */
#define BEHIND     8        /* 左側に確保しておく描き込み済み幅(MSK の 8 ドット分) */

typedef struct {
    u16 idle_base;
    u16 idle_min;
    u16 idle_last;
    u16 miss;
    u16 cols;
    u16 frame;
    u16 pos;        /* VBLANK で表示に適用した位置(世界座標ドット) */
    s16 speed;      /* 現在速度(1/256 ドット/フレーム) */
    u8  seg;        /* 速度プログラムの区間番号 */
} M0Stat;

volatile M0Stat g_m0;

/* 検証用: デバッガから freeze=1 と pos を書くと、速度プログラムを止めてその位置に固定する
   (R#26/R#27 換算を 1 ドット単位で照合するテスト tools/test/run_m0_scroll.sh が使う)。 */
volatile u8  g_dbg_freeze;
volatile u16 g_dbg_pos;

static volatile u8  pend_r26, pend_r27;
static volatile u16 pend_pos;

/* ---- VBLANK: 準備済みの横スクロール値を適用 ---- */
static void vblank(void) {
    VDP_ISR_WREG(26, pend_r26);
    VDP_ISR_WREG(27, pend_r27);
    g_m0.pos = pend_pos;
}

/* 世界座標 x(ドット)をリング表示位置にするレジスタ値。
   R#26 は 8 ドット単位(SP2 で 6bit = 512 ドット)、R#27 は逆向きの細かい補正。 */
static void set_scroll(u16 x) {
    u8 r26 = (u8)(((x + 7) >> 3) & 63);
    u8 r27 = (u8)((0u - x) & 7);
    __asm di __endasm;
    pend_r26 = r26;
    pend_r27 = r27;
    pend_pos = x;
    __asm ei __endasm;
}

/* ---- 世界(地形)の定義: 列番号 c(8 ドット単位)→ ストリップ番号 0..31 ---- */
static u8 strip_of(u16 c) {
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

static void gen_strips(void) {
    u8 y, s, x;
    for (y = 0; y < 212; y++) {
        vdp_write_addr17(1, (u16)y * 128u);        /* ページ 2 = VRAM 0x10000 */
        for (s = 0; s < 32; s++)
            for (x = 0; x < 8; x += 2)
                VDP_DAT = (u8)((strip_pixel(s, x, y) << 4) | strip_pixel(s, (u8)(x + 1), y));
    }
}

/* ---- 列ストリーミング: 描き込み済み範囲 [lo, hi)(世界座標、8 の倍数、16bit 巡回) ---- */
static u16 lo, hi;

static void draw_col(u16 wx) {
    u16 rx = wx & (RING_W - 1);
    u8  s  = strip_of(wx >> 3);
    vdp_hmmm((u16)s * 8u, PAGE2_Y, rx & 255u, (rx & 256u) ? 256u : 0u, 8, 212);
    g_m0.cols++;
}

static void stream(u16 x) {
    u8 n = 0;
    /* 描き込み済み範囲の外へ飛んだ(ワープ)ら、間を順に描かずにリングを張り直す */
    if ((s16)(x - lo) < 0 || (s16)(x - hi) >= 0) lo = hi = x & ~7u;
    while ((s16)(hi - x) < AHEAD && n < 4) {
        if ((u16)(hi - lo) >= RING_W) lo += 8;
        draw_col(hi);
        hi += 8;
        n++;
    }
    while ((s16)(x - lo) < BEHIND && n < 8) {
        if ((u16)(hi - lo) >= RING_W) hi -= 8;
        lo -= 8;
        draw_col(lo);
        n++;
    }
}

/* ---- 速度プログラム ---- */
typedef struct { s16 target; s16 accel; u8 hold; } Seg;
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

static void palette(void) {
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

static void screen_init(void) {
    __asm
        ld   a, #5
        ld   (0xFCAF), a     ; SCRMOD
        call 0x005F          ; CHGMOD
    __endasm;
    vdp_wreg(8, 0x2A);       /* TP=1(色0 を透明にせずパレット0で描く: 枠線色の負荷表示が背景へ透けない),
                                VR=1, SPD=1(スプライト非表示: M0-1 では使わない) */
    vdp_wreg(9, 0x80);       /* LN=1(212 ライン), NTSC */
    vdp_wreg(2, 0x3F);       /* SP2: 表示はページ 1 指定でページ 0|1 の 512 幅 */
    vdp_wreg(25, 0x03);      /* SP2 | MSK */
    vdp_wreg(26, 0);
    vdp_wreg(27, 0);
    vdp_wreg(7, 0);
    palette();
}

void main(void) {
    u16 posi = 0;
    u8  posf = 0;
    s16 speed = 0;
    u8  seg = 0, hold = 0;
    u8  i;

    sys_r800();
    screen_init();
    vdp_hmmv(0, 0, 256, 512, 0x00);           /* ページ 0-1 クリア */
    gen_strips();
    lo = hi = 0;
    for (i = 0; i < 16; i++) stream(0);       /* 表示前にリングを埋め切る(stream は 1 回の列数に上限がある) */
    vdp_cmd_wait();
    set_scroll(0);
    sys_hook_vblank(vblank);

    /* 基準のアイドル回数(何もしないフレーム) */
    frame_begin();
    frame_sync(1);
    g_m0.idle_base = 0;
    for (i = 0; i < 8; i++) {
        u16 t = frame_sync(1);
        if (t > g_m0.idle_base) g_m0.idle_base = t;
    }
    g_m0.idle_min = 0xFFFF;

    for (;;) {
        const Seg *sg = &prog[seg];
        u16 idle;

        if (g_dbg_freeze) { posi = g_dbg_pos; posf = 0; speed = 0; goto apply; }

        /* 速度を目標へ */
        if (speed < sg->target) { speed += sg->accel; if (speed > sg->target) speed = sg->target; }
        else if (speed > sg->target) { speed -= sg->accel; if (speed < sg->target) speed = sg->target; }
        else if (++hold >= sg->hold) { hold = 0; if (++seg >= NSEG) seg = 0; }

        /* 位置を進める(1/256 ドット固定小数点) */
        {
            s16 acc = (s16)posf + speed;
            posi += (u16)(acc >> 8);
            posf = (u8)acc;
        }
    apply:

        vdp_wreg(7, 4);
        stream(posi);
        vdp_cmd_wait();                        /* 次の VBLANK までに描き終えていることを保証 */
        vdp_wreg(7, 0);
        set_scroll(posi);

        g_m0.speed = speed;
        g_m0.seg = seg;
        g_m0.frame++;
        idle = frame_sync(1);              /* 絶対時刻同期: 遅れたフレームは待たずに進む */
        g_m0.miss = g_frame_late;
        g_m0.idle_last = idle;
        if (idle < g_m0.idle_min) g_m0.idle_min = idle;
    }
}
