/* hud.c — HUD 帯(画面下 24 行、スクロールしない)と∞ゲージ(指示書 2.3 / 2.4)。
   左半分: ∞の軌道と 7 つのノード(tools/gen_hud.py の絵)。カーソルの光点はスプライト。
   右半分: スコア / 残機 / カーソルの指すノードと段階。変わったところだけ書く(VDP への書き込みは 1 バイト 8.6us)。
   ★確定時の「∞が拡大回転して画面中央に飛び出す」演出は回転拡縮エンジン ROTOR(M3)で入れる。今は光点の点滅のみ。 */
#include "game.h"
#include "snd.h"
#include "vdp.h"
#include "font.h"
#include "spr.h"
#include "hud_data.h"
#include "sprite_data.h"

Gauge g_gauge;

#define TX 136                       /* 右側の文字の左端 */
#define GX 8                         /* ゲージの絵の左端(左 8 ドットは MSK で隠れる) */

static u16 shown_score = 0xFFFF;
static u8  shown_lives = 0xFF;
static u8  shown_node = 0xFF, shown_lv = 0xFF, shown_armed = 0xFF;

static const char *const node_name[N_STEPS] = {
    "INF", "THRUST", "ECHO", "AEGIS", "INF", "DROP", "SPLIT", "RAY" };
static const u8 node_max[N_STEPS] = { 0, 5, 4, 2, 0, 2, 2, 2 };

static u8 *level_of(u8 node) {
    switch (node) {
    case N_THRUST: return &g_gauge.thrust;
    case N_ECHO:   return &g_gauge.echo;
    case N_AEGIS:  return &g_gauge.aegis;
    case N_DROP:   return &g_gauge.drop;
    case N_SPLIT:  return &g_gauge.split;
    case N_RAY:    return &g_gauge.ray;
    default:       return 0;
    }
}

/* ---- 文字の書き込み待ち行列 ----
   ★文字の書き換えは VDP の I/O が多く(15 文字で約 70 ライン)、1 フレームで書くとそのフレームが遅れた。
     項目ごとに文字列を用意し、1 フレームに 2 行(8 行のうち)ずつ書き進める。 */
typedef struct { char text[16]; u8 x, y, fg; u8 row; } TextJob;   /* row = 8 で書き終わり */
enum { J_SCORE, J_LIVES, J_NODE, NJOB };
static TextJob job[NJOB];

static void job_set(u8 j, u8 x, u8 y, u8 fg, const char *s, u8 width) {
    TextJob *t = &job[j];
    u8 i = 0;
    while (s[i] && i < width) { t->text[i] = s[i]; i++; }
    while (i < width) t->text[i++] = ' ';
    t->text[width] = 0;
    t->x = x; t->y = y; t->fg = fg; t->row = 0;
}

/* HUD を描き直す。★一度に描くと 1 フレームに収まらない(場面の切り替えでフレーム落ち)ので、
   消去だけ始めて、残りは hud_update が 1 フレームに 1 段ずつ描く(init_stage)。 */
static u8 init_stage;

void hud_init(void) {
    vdp_hmmv(0, 512 + HUD_VROW, 256, 24, 0x00);             /* ページ 2 の HUD の行 */
    shown_score = 0xFFFF; shown_lives = 0xFF; shown_node = 0xFF;
    { u8 j; for (j = 0; j < NJOB; j++) job[j].row = 8; }
    init_stage = 1;
}

#define GAUGE_STEP 2                 /* ゲージの絵を 1 フレームに描く行数(1 行 64 回の書き込み ≒ 0.55ms。★4 行だと出撃の声と重なって遅れた) */
#define INIT_TEXT  (GAUGE_H / GAUGE_STEP + 1)

static void init_step(void) {
    vdp_cmd_wait();
    if (init_stage < INIT_TEXT) {
        u8 y, x, y0 = (u8)((init_stage - 1) * GAUGE_STEP);
        for (y = y0; y < y0 + GAUGE_STEP; y++) {
            vdp_write_addr17(1, (u16)(HUD_VROW + y) * 128u + GX / 2);
            for (x = 0; x < GAUGE_W / 2; x++) VDP_DAT = hud_gauge[y][x];
        }
    } else if (init_stage == INIT_TEXT) {
        font_text(HUD_PAGE, TX, HUD_ROW0, 14, 0, "SC");
        font_text(HUD_PAGE, (u8)(TX + 64), HUD_ROW0, 15, 0, "0");       /* スコアは 10 点単位なので末尾の 0 は固定 */
    } else {
        font_text(HUD_PAGE, TX, HUD_ROW1, 14, 0, "SHIP");
    }
    init_stage = init_stage > INIT_TEXT ? 0 : (u8)(init_stage + 1);
}

static void jobs_run(u8 rows) {
    u8 j;
    for (j = 0; j < NJOB && rows; j++) {
        TextJob *t = &job[j];
        while (t->row < 8 && rows) { font_text_row(HUD_PAGE, t->x, t->y, t->fg, 0, t->text, t->row); t->row++; rows--; pcmx_service(); }   /* ★1 行ごとに PCM の送出口(M7) */
    }
}

static void num_str(char *buf, u16 v, u8 digits) {
    u8 i;
    buf[digits] = 0;
    for (i = digits; i > 0; i--) { buf[i - 1] = (char)('0' + v % 10u); v /= 10u; }
}

/* 変わった項目を待ち行列へ入れ、1 フレームに rows 行ずつ書く */
void hud_update(u8 rows) {
    u8 node = g_gauge.cursor;
    u8 *lv = level_of(node);
    u8 lvv = lv ? *lv : 0;
    char buf[16];
    if (init_stage) { init_step(); return; }
    /* ★得点は描きかけを描き終えてから次の値を取る(変わるたびに 0 行目から描き直していた間は、ボス戦で毎フレーム得点が
       入ると上の 2 行だけを書き続け、数字が新旧混ざったまま 1 フレームに 2 行の仕事が続いた。8 面の第 2 形態で遅れた) */
    if (g_pl.score != shown_score && job[J_SCORE].row >= 8) {
        shown_score = g_pl.score;
        num_str(buf, g_pl.score, 5);
        job_set(J_SCORE, (u8)(TX + 24), HUD_ROW0, 15, buf, 5);
    }
    if (g_pl.lives != shown_lives) {
        shown_lives = g_pl.lives;
        num_str(buf, g_pl.lives, 1);
        job_set(J_LIVES, (u8)(TX + 40), HUD_ROW1, 15, buf, 1);
    }
    if (node != shown_node || lvv != shown_lv || g_gauge.armed != shown_armed) {
        shown_node = node; shown_lv = lvv; shown_armed = g_gauge.armed;
        if (node == N_INF || node == N_INF2) {
            job_set(J_NODE, TX, HUD_ROW2, g_gauge.armed ? 11 : 14, g_gauge.armed ? "LIMIT BREAK" : "----", 11);
        } else {
            const char *nm = node_name[node];
            u8 i = 0, mx = node_max[node];
            while (nm[i]) { buf[i] = nm[i]; i++; }
            while (i < 8) buf[i++] = ' ';
            buf[8] = (char)('0' + lvv); buf[9] = '-'; buf[10] = (char)('0' + mx); buf[11] = 0;
            job_set(J_NODE, TX, HUD_ROW2, lvv >= mx ? 14 : 11, buf, 11);
        }
    }
    jobs_run(rows);
}

void hud_sprites(void) {
    u8 x, y;
    if (g_gauge.anim) {
        u8 from = (u8)((g_gauge.cursor + N_STEPS - 1) % N_STEPS);
        u8 k = (u8)(PATH_STEPS - g_gauge.anim);
        x = gauge_path[from][k][0];
        y = gauge_path[from][k][1];
        g_gauge.anim--;
    } else {
        x = gauge_node[g_gauge.cursor][0];
        y = gauge_node[g_gauge.cursor][1];
    }
    if (g_gauge.flash) {
        g_gauge.flash--;
        if (g_gauge.flash & 4) return;          /* 確定の演出: 点滅 */
    }
    spr_put_hud((s16)x + GX - 7, (s16)(HUD_Y + y) - 7, PAT_CURSOR, g_gauge.armed ? CS_CURSOR : CS_DIM);
}

/* ---- ゲージの操作 ---- */
void gauge_quanta(void) {
    g_gauge.cursor = (u8)((g_gauge.cursor + 1) % N_STEPS);
    g_gauge.armed = 1;
    sfx(SFX_ITEM);
    g_gauge.anim = PATH_STEPS;
}

void gauge_shard(void) {
    g_gauge.cursor = N_INF;
    g_gauge.armed = 1;
    g_gauge.anim = 0;
    sfx(SFX_SHARD);
}

static void down1(u8 *v) { if (*v) (*v)--; }

void gauge_confirm(void) {
    u8 node = g_gauge.cursor;
    u8 *lv = level_of(node);
    if (!g_gauge.armed) return;
    if (node == N_INF || node == N_INF2) {
        /* LIMIT BREAK: 一掃。引き換えに THRUST 以外の強化が 1 段ずつ下がる */
        limit_break();
        sfx(SFX_LIMIT);
        down1(&g_gauge.echo); down1(&g_gauge.aegis); down1(&g_gauge.drop);
        down1(&g_gauge.split); down1(&g_gauge.ray);
    } else {
        if (*lv >= node_max[node]) { sfx(SFX_DENY); return; }   /* 上限: 確定しない */
        (*lv)++;
        if (node == N_SPLIT) g_gauge.ray = 0;   /* SPLIT と RAY は排他 */
        if (node == N_RAY) g_gauge.split = 0;
    }
    g_gauge.cursor = N_INF;
    g_gauge.armed = 0;
    g_gauge.anim = 0;
    g_gauge.flash = 32;
    infx_start(128, 172);                       /* ∞ が回りながら画面の中央へ飛び出す(ROTOR) */
}

void gauge_lose(void) {
    u8 f = g_gauge.flash;
    g_gauge.thrust = g_gauge.echo = g_gauge.aegis = g_gauge.drop = g_gauge.split = g_gauge.ray = 0;
    g_gauge.cursor = N_INF;
    g_gauge.armed = 0;
    g_gauge.anim = 0;
    g_gauge.flash = f;
}
