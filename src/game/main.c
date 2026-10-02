/* main.c — Criterion 本体(M1: 1 面の空中戦が 60Hz スクロールで遊べるところまで)。
   起動 → 背景・HUD・スプライトの初期化(BIOS の CHGMOD を使うのはここまで)→ RAM 実行・直接割込み →
   毎フレーム: 入力 → 自機 → 敵 → スクロール → スプライト → ラスターリスト → HUD。
   ★ステージ開始表示・タイトル・出撃シーンは M2(指示書 9)。今は起動してすぐ空中戦が始まる。 */
#include "types.h"
#include "vdp.h"
#include "sys.h"
#include "frame.h"
#include "raster.h"
#include "spr.h"
#include "input.h"
#include "font.h"
#include "game.h"
#include "sprite_data.h"

enum { ST_PLAY, ST_OVER };

/* 計測・検証用(デバッガから読む / 書く) */
typedef struct {
    u16 base;          /* 何もしないフレームのアイドル回数 */
    u16 idle_min;
    u16 late;          /* 遅れたフレーム */
    u16 frames;
    u32 idle_sum;
    u8  state;
    u8  dropped;       /* スプライトが表に入りきらなかった累計(255 で止める) */
} GameStat;
volatile GameStat g_stat;
volatile u8 g_dbg_invul;      /* 1 = 被弾しない(自動テスト用) */
/* 段ごとの所要時間(S1990 タイマのカウント。1 ライン ≒ 16.27): 入力・自機・敵・スプライト・ラスター・HUD */
#define NPROF 6
volatile u16 g_prof_max[NPROF];
volatile u32 g_prof_sum[NPROF];
static u16 prof_t;
static void prof(u8 k) {
    u16 now = sys_timer(), d = (u16)(now - prof_t);
    prof_t = now;
    if (d > g_prof_max[k]) g_prof_max[k] = d;
    g_prof_sum[k] += d;
}

static u8 state;

static void new_game(void) {
    player_init();
    enemy_init();
    g_gauge.cursor = N_INF; g_gauge.armed = 0; g_gauge.anim = 0; g_gauge.flash = 0;
    gauge_lose();
    hud_init();
    state = ST_PLAY;
}

static void screen_init(void) {
    __asm
        ld   a, #5
        ld   (0xFCAF), a
        call 0x005F          ; CHGMOD
    __endasm;
    vdp_wreg(8, 0x28);       /* TP, VR、スプライト表示 */
    vdp_wreg(9, 0x80);       /* 212 ライン、NTSC */
    vdp_wreg(2, 0x3F);       /* SP2: ページ 0|1 */
    vdp_wreg(25, 0x03);      /* SP2 | MSK */
    vdp_wreg(7, 0);
}

void main(void) {
    sys_r800();
    screen_init();
    vdp_hmmv(0, 0, 256, 512, 0x00);
    vdp_cmd_wait();
    bg_init();
    spr_init(spr_pat, PAT_COUNT);
    sys_ram_exec();
    ras_init();
    ras_set_isr(RAS_ISR_DIRECT);
    new_game();

    frame_begin();
    { u8 j; u16 m = 0; frame_sync(1); for (j = 0; j < 8; j++) { u16 v = frame_sync(1); if (v > m) m = v; } g_stat.base = m; }
    g_stat.idle_min = 0xFFFF;

    for (;;) {
        u16 late0 = g_frame_late, idle;
        idle = frame_sync(1);
        if (g_frame_late != late0) g_stat.late++;
        if (idle < g_stat.idle_min) g_stat.idle_min = idle;
        g_stat.frames++;
        g_stat.idle_sum += idle;

        prof_t = sys_timer();
        input_read();
        prof(0);
        if (state == ST_PLAY) {
            if (g_dbg_invul) g_pl.invul = 2;
            player_update();
            prof(1);
            enemy_update();
            prof(2);
            if (!g_pl.alive && !g_pl.dead_t && !g_pl.lives) {
                state = ST_OVER;
                font_text(0, 136, HUD_ROW1, 8, 0, "GAME OVER");
            }
        } else if (g_in_edge & IN_A) {
            new_game();
        }
        bg_scroll();

        spr_begin();
        player_draw();
        enemy_draw();
        hud_sprites();
        spr_end();
        prof(3);
        bg_build();
        prof(4);
        hud_update();
        prof(5);

        g_stat.state = state;
        if (g_spr_dropped && g_stat.dropped < 255) { g_stat.dropped++; g_spr_dropped = 0; }
    }
}
