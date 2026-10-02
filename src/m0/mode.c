/* mode.c(M0) — M0 技術検証 (4): SCREEN12(YJK 自然画)と SCREEN5 の画面分割の境界。指示書 8.10 / 9(M0-4)。

   画面: 0..N-1 行 = SCREEN12(GRAPHIC7 + YJK)。横にゆっくりスクロールする(タイトルの地平線、6 面の惑星)。
         N..211 行 = SCREEN5。M0-1 の世界を速度プログラムでスクロールする。N は 48(6 面)/ 64(タイトル)。
   VRAM: ★GRAPHIC7 の表示は VRAM を 2 つの 64KB バンクに 1 バイトずつ交互に読む(表示アドレス A → 物理
         (A>>1) | ((A&1)<<16))。SCREEN5 の単位で言うと、上の帯 N 行は「ページ 0 の 0..N-1 行(偶数バイト)」と
         「ページ 2 の 0..N-1 行(奇数バイト)」を使う(M0-4 で、一直線に書いて半分が別のページを読んだ)。
         SCREEN5 のリング = ページ 0|1(R#2 = 0x3F)。列は N 行目以降だけ描く。列ストリップ = ページ 3、
         スプライト = ページ 1 の 224 行以降(パターン 0xF000、色 0xF800、属性 0xFA00)。
   境界(ラスターリストの RAS_REGS)で R#0(モード)、R#25(YJK / SP2 / MSK)、R#2(表示ページ)、R#8(スプライト
   表示の可否 SPD)、R#26/R#27(横スクロール)を書き換える。SCREEN12 の帯ではスプライトの色の解釈が変わるので
   SPD で消す(指示書 8.10)。スプライト 8 枚を境界をまたいで上下させ、帯の中で消えているかを見る。

   g_cfg(デバッガ / キー):
     split  0 = 全面 SCREEN5、1 = 分割、2 = 全面 SCREEN12(比較の基準を撮るため)
     n      分割の行(48 / 64)
     order  境界での書き換えの順(0: R#8,R#25,R#0,R#2,R#26,R#27 / 1: R#0,R#25,R#2,R#8,R#26,R#27)
     yx     SCREEN12 の帯の横位置(freeze 中はこの値に固定)
     pcm    1 = PCM 方式 C を鳴らす(刻み方式でも境界が崩れないか)
     freeze 1 = 時間を止める(画素照合用)
   操作(実機用): 0/1/2 = split / ←→ = 分割の行 48/64 / SPACE = 書き換えの順 / P = PCM */
#include "types.h"
#include "vdp.h"
#include "sys.h"
#include "frame.h"
#include "raster.h"
#include "world.h"
#include "pcm_data.h"

#define AHEAD     264
#define BEHIND    8

typedef struct {
    u8  split;
    u8  n;
    u8  order;
    u8  pcm;
    u8  freeze;
    u16 yx;
} M4Cfg;

typedef struct {
    u16 base;
    u16 idle_min;
    u16 late;
    u16 frames;
    u32 idle_sum;
    u16 t;
    u16 wx;        /* SCREEN5 の帯の横位置 */
    u16 yx;        /* SCREEN12 の帯の横位置 */
    u8  spr_y;     /* スプライトの基準 Y */
} M4Stat;

volatile M4Cfg  g_cfg = { 1, 48, 0, 0, 0, 0 };
volatile M4Stat g_m4;

/* ---- レジスタ値 ---- */
#define R0_G4   0x16   /* GRAPHIC4 + IE1 */
#define R0_G7   0x1E   /* GRAPHIC7 + IE1 */
#define R25_G4  0x03   /* SP2 | MSK */
#define R25_YJK 0x0A   /* YJK | MSK(SP2 なし: GRAPHIC7 の 2 ページ連結は VRAM が足りない) */
#define R2_G4   0x3F   /* SP2: ページ 0|1 */
#define R2_G7   0x1F   /* GRAPHIC7 のページ 0 */
#define R8_SPR  0x28   /* TP | VR、スプライト表示 */
#define R8_NOSP 0x2A   /* TP | VR | SPD */

/* 帯の切り替えのレジスタ列(ダブルバッファ: 表と同じく裏で組む) */
static u8 regs_top[2][12], regs_bot[2][12];

#define REG_PAIRS 6

static u8 fill_regs(u8 *r, u8 order, u8 r0, u8 r25, u8 r2, u8 r8, u16 x) {
    static const u8 ord0[4] = { 8, 25, 0, 2 }, ord1[4] = { 0, 25, 2, 8 };
    const u8 *o = order ? ord1 : ord0;
    u8 i, k = 0;
    for (i = 0; i < 4; i++) {
        u8 reg = o[i];
        r[k++] = reg;
        r[k++] = reg == 0 ? r0 : reg == 25 ? r25 : reg == 2 ? r2 : r8;
    }
    r[k++] = 26; r[k++] = R26(x);
    r[k++] = 27; r[k] = R27(x);
    return REG_PAIRS;
}

/* ★VRAM を触ってよいのは SCREEN5 を表示している区間だけ。GRAPHIC7 の表示中は CPU の VRAM アクセスにも
   インターリーブの変換がかかり、VDP コマンドの座標の解釈も変わる(M0-4 で、上の帯の表示中に書いた絵と
   列の描き込みが壊れた)。そこで:
     - 上の帯への切り替え(GRAPHIC7)は VBLANK ではなく 244 行(R#19 = 240)で行う。★PCM の刻み 244 は
       穴埋め(16 ライン)をするので重い項目を置かない(置くと穴埋めの終わりが後ろへずれ、刻みの鎖が切れた)
     - 下の帯への切り替え(n 行)に RAS_MARK を付け、メインはそれを待ってから VRAM を書く(244 行までに終える)
   表の並びは割込みの順(VBLANK の後 = 212..244 → 0..211)なので、244 行の項目が先、n 行の項目が後。 */
#define TOP_SWITCH_LINE 244

static volatile u8 layout_busy;   /* 1 = 描き直し中(全面 SCREEN5 にする) */

static void build(void) {
    RasEntry *e = ras_back();
    u8 bi = ras_back_index();
    u8 k = 0;
    u8 *rt = regs_top[bi], *rb = regs_bot[bi];
    u8 split = layout_busy ? 0 : g_cfg.split;
    if (split == 0) {                                  /* 全面 SCREEN5 */
        e[k].line = 0; e[k].flags = RAS_REGS;
        e[k].haze_n = fill_regs(rt, g_cfg.order, R0_G4, R25_G4, R2_G4, R8_SPR, g_m4.wx);
        e[k].haze = rt;
        k++;
    } else {
        e[k].line = split == 1 ? TOP_SWITCH_LINE : 0;   /* 全面 SCREEN12 は VBLANK で */
        e[k].flags = RAS_REGS;
        e[k].haze_n = fill_regs(rt, g_cfg.order, R0_G7, R25_YJK, R2_G7, R8_NOSP, g_m4.yx);
        e[k].haze = rt;
        k++;
        if (split == 1) {
            e[k].line = g_cfg.n; e[k].flags = RAS_REGS | RAS_MARK;
            e[k].haze_n = fill_regs(rb, g_cfg.order, R0_G4, R25_G4, R2_G4, R8_SPR, g_m4.wx);
            e[k].haze = rb;
            k++;
        }
    }
    ras_commit(k);
}

/* 下の帯(SCREEN5)の表示に入るまで待つ。分割していなければ待たない。全面 SCREEN12 では 0 を返す(書かない)。 */
static u8 wait_vram_window(void) {
    u8 m0, i;
    if (layout_busy || g_cfg.split == 0) return 1;
    if (g_cfg.split == 2) return 0;
    m0 = g_ras_mark;
    for (i = 0; i < 200 && g_ras_mark == m0; i++) {      /* 見張り: 来なければ 1 フレームほどで諦める */
        u16 t0 = sys_timer();
        while (g_ras_mark == m0 && (u16)(sys_timer() - t0) < 30) { }
    }
    return g_ras_mark != m0;
}

/* ---- SCREEN12 の帯の絵: YJK で合成(輝度 = 縦の空のグラデーション、色相 = 横に変化、32 ドットごとに明るい縦線) ----
   YJK は 4 画素で J/K を共有: byte0 = Y0<<3 | K下位3、byte1 = Y1<<3 | K上位3、byte2 = Y2<<3 | J下位3、byte3 = Y3<<3 | J上位3 */
static const s8 hue_j[16] = { 0, 8, 15, 20, 22, 20, 15, 8, 0, -8, -15, -20, -22, -20, -15, -8 };
static u8 yjk_byte(u8 y, u8 x) {
    u8 gx = x >> 2, i = x & 3;
    s8 j = hue_j[gx & 15];
    s8 kk = hue_j[(gx + 4) & 15];
    u8 yy = (u8)(4 + (y >> 2) + ((gx & 31) < 2 ? 10 : 0));   /* 下ほど明るい + 縦線 */
    u8 lo;
    if (y > 40 && y < 44) yy = 30;                          /* 地平線 */
    lo = (u8)(i == 0 ? (kk & 7) : i == 1 ? ((kk >> 3) & 7) : i == 2 ? (j & 7) : ((j >> 3) & 7));
    return (u8)((yy << 3) | lo);
}

/* 0..n-1 行を描く。偶数バイトはページ 0(VRAM 0x00000 + y*128)、奇数バイトはページ 2(0x10000 + y*128)。 */
static void gen_yjk(u8 n) {
    u8 y, x;
    for (y = 0; y < n; y++) {
        vdp_write_addr17(0, (u16)y * 128u);
        x = 0; do { VDP_DAT = yjk_byte(y, x); x += 2; } while (x);
        vdp_write_addr17(1, (u16)y * 128u);
        x = 1; do { VDP_DAT = yjk_byte(y, x); x += 2; } while (x != 1);
    }
}

/* ---- スプライト 8 枚(16x16 の四角)。境界をまたいで上下する ---- */
static void sprites_init(void) {
    u8 i;
    vdp_write_addr17(0, 0xF000);                 /* パターン 0: 16x16 塗りつぶし(32 バイト) */
    for (i = 0; i < 32; i++) VDP_DAT = 0xFF;
    vdp_write_addr17(0, 0xF800);                 /* 色(1 枚 16 行) */
    for (i = 0; i < 8 * 16; i++) VDP_DAT = (u8)(8 + (i >> 4));
    vdp_wreg(6, 0x1E);                           /* パターン 0xF000 */
    vdp_wreg(5, 0xF7);                           /* 属性 0xFA00(色 = 0xF800) */
    vdp_wreg(11, 0x01);
    vdp_wreg(1, (u8)(*(volatile u8 *)0xF3E0 | 0x02));   /* 16x16 */
    *(volatile u8 *)0xF3E0 |= 0x02;
}

static void sprites_put(u8 base_y) {
    u8 i;
    vdp_write_addr17(0, 0xFA00);
    for (i = 0; i < 8; i++) {
        VDP_DAT = (u8)(base_y + (i & 3) * 6 - 1);   /* Y(表示は +1) */
        VDP_DAT = (u8)(24 + i * 28);                /* X */
        VDP_DAT = 0;                                /* パターン */
        VDP_DAT = 0;
    }
    VDP_DAT = 216;                                  /* 終わり */
}

static void palette(void) {
    world_palette();
    vdp_set_pal(8, 7, 0, 0);  vdp_set_pal(9, 0, 7, 0);
    vdp_set_pal(12, 0, 0, 7); vdp_set_pal(13, 7, 7, 0);
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

__sfr __at(0xA5) PCM_CTRL;

/* 分割の行 n に合わせて、上の帯の絵とリングを描き直す(列は n 行目以降だけ描く)。
   ★描き直しの間は全面 SCREEN5 にする(GRAPHIC7 の表示中に書くと壊れる)。 */
static void layout(u8 n) {
    u8 i;
    layout_busy = 1;
    for (i = 0; i < 3; i++) { build(); frame_sync(1); }
    vdp_cmd_wait();
    gen_yjk(n);
    world_set_top(n);
    world_reset(g_m4.wx);
    for (i = 0; i < 16; i++) world_stream(g_m4.wx, AHEAD, BEHIND);
    vdp_cmd_wait();
    layout_busy = 0;
}

void main(void) {
    WorldRun run = { 0, 0, 0, 0, 0 };
    u16 t = 0;
    u8 i, prev0 = 0, prev8 = 0, prev4 = 0, skip = 2, pcm_now = 0;
    M4Cfg last;

    sys_r800();
    __asm
        ld   a, #5
        ld   (0xFCAF), a
        call 0x005F          ; CHGMOD(BIOS を使うのは初期化だけ)
    __endasm;
    vdp_wreg(8, R8_SPR);
    vdp_wreg(9, 0x80);
    vdp_wreg(2, R2_G4);
    vdp_wreg(25, R25_G4);
    vdp_wreg(7, 0);
    palette();
    vdp_hmmv(0, 0, 256, 1024, 0x00);             /* VRAM 全体(4 ページ)を消す */
    vdp_cmd_wait();
    sprites_init();
    sprites_put(40);
    world_set_pages(0, 3);
    world_gen_strips();
    gen_yjk(g_cfg.n);                            /* まだ割込みを使っていない(全面 SCREEN5)ので直接描く */
    world_set_top(g_cfg.n);
    world_reset(0);
    for (i = 0; i < 16; i++) world_stream(0, AHEAD, BEHIND);
    vdp_cmd_wait();
    sys_ram_exec();
    PCM_CTRL = (u8)((PCM_CTRL & 0x04) | 0x02);
    ras_init();
    ras_set_isr(RAS_ISR_DIRECT);
    frame_begin();
    { u8 j; u16 m = 0; frame_sync(1); for (j = 0; j < 8; j++) { u16 v = frame_sync(1); if (v > m) m = v; } g_m4.base = m; }
    last = g_cfg;

    for (;;) {
        u8 k0 = key_row(0), k8 = key_row(8), k4 = key_row(4);
        u8 e0 = (u8)(k0 & ~prev0), e8 = (u8)(k8 & ~prev8), e4 = (u8)(k4 & ~prev4);
        u16 idle;
        prev0 = k0; prev8 = k8; prev4 = k4;
        for (i = 0; i < 3; i++) if (e0 & (1 << i)) g_cfg.split = i;    /* 0..2 */
        if (e8 & 0x80) g_cfg.n = 64;                                    /* → */
        if (e8 & 0x10) g_cfg.n = 48;                                    /* ← */
        if (e8 & 0x01) g_cfg.order ^= 1;                                /* SPACE */
        if (e4 & 0x08) g_cfg.pcm ^= 1;                                  /* P(行 4 bit3) */

        if (g_cfg.pcm != pcm_now) {
            pcm_now = g_cfg.pcm;
            if (pcm_now) pcm_play(pcm_hit, PCM_HIT_LEN, 1); else pcm_stop();
        }
        if (pcm_now && !pcm_queued()) pcm_queue(pcm_hit, PCM_HIT_LEN);
        if (g_cfg.n != last.n) layout(g_cfg.n);
        if (g_cfg.split != last.split || g_cfg.n != last.n || g_cfg.order != last.order || g_cfg.pcm != last.pcm) {
            last = g_cfg;
            g_m4.idle_min = 0xFFFF; g_m4.late = 0; g_m4.frames = 0; g_m4.idle_sum = 0;
            skip = 2;
        }

        if (!g_cfg.freeze) {
            world_speed_step(&run);
            t++;
            g_m4.yx = (u16)(t >> 2);                  /* SCREEN12 の帯は 4 フレームに 1 ドット */
            g_m4.spr_y = (u8)(g_cfg.n - 12 + ((t >> 1) & 15));
        } else {
            g_m4.yx = g_cfg.yx;
        }
        g_m4.t = t;
        g_m4.wx = run.posi;
        build();
        if (wait_vram_window()) {                /* VRAM は下の帯の表示中にだけ書く */
            /* ★フレーム待ちの基準を置き直す。ループの位相が n 行目(合図)に揃うので、そのままだと
               フレーム待ちに入る時点で毎回 VBLANK を過ぎていて「遅れ」扱いのまま即座に抜け続けた
               (60fps では回るが、待ちが VBLANK に揃わない)。 */
            if (g_cfg.split == 1 && !layout_busy) frame_begin();
            world_stream(run.posi, AHEAD, BEHIND);
            sprites_put(g_m4.spr_y);
            vdp_cmd_wait();
        }

        {
            u16 late0 = g_frame_late;
            idle = frame_sync(1);
            if (skip) skip--;
            else {
                if (g_frame_late != late0) g_m4.late++;
                if (idle < g_m4.idle_min) g_m4.idle_min = idle;
                g_m4.frames++;
                g_m4.idle_sum += idle;
            }
        }
    }
}
