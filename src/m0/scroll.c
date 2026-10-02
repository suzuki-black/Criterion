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
#include "world.h"

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
    world_palette();
}

void main(void) {
    WorldRun run = { 0, 0, 0, 0, 0 };
    u8  i;

    sys_r800();
    screen_init();
    vdp_hmmv(0, 0, 256, 512, 0x00);           /* ページ 0-1 クリア */
    vdp_cmd_wait();
    world_gen_strips();
    world_reset(0);
    for (i = 0; i < 16; i++) world_stream(0, AHEAD, BEHIND);       /* 表示前にリングを埋め切る(stream は 1 回の列数に上限がある) */
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
        u16 idle;

        if (g_dbg_freeze) { run.posi = g_dbg_pos; run.posf = 0; run.speed = 0; }
        else world_speed_step(&run);

        vdp_wreg(7, 4);
        world_stream(run.posi, AHEAD, BEHIND);
        vdp_cmd_wait();                        /* 次の VBLANK までに描き終えていることを保証 */
        vdp_wreg(7, 0);
        set_scroll(run.posi);

        g_m0.cols = g_world_cols;
        g_m0.speed = run.speed;
        g_m0.seg = run.seg;
        g_m0.frame++;
        idle = frame_sync(1);              /* 絶対時刻同期: 遅れたフレームは待たずに進む */
        g_m0.miss = g_frame_late;
        g_m0.idle_last = idle;
        if (idle < g_m0.idle_min) g_m0.idle_min = idle;
    }
}
