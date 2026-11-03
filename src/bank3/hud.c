/* hud.c(第 3 バンク)— HUD 帯の表示(指示書 2.3 / 2.4)。左半分: ∞の軌道と 7 つのノード(tools/gen_hud.py の絵)。カーソルの光点は
   スプライト。右半分: スコア / 残機 / カーソルの指すノードと段階。変わったところだけ書く(VDP への書き込みは 1 バイト 8.6us)。
   ★第 3 バンク(bank.h): 常駐の空きを作るため、表示の側とゲージの絵の表(約 1.7KB)をここへ移した。ゲージの変数(g_gauge)と
     増減(gauge_*。第 2 バンクの infx_start を呼ぶので入れ子になる)は本体の src/game/hud.c。
   ★変数はセグメント 7 の中(0xBE00-)で初期値が付かないので、hud_init で全部を代入する */
#include "game.h"
#include "snd.h"
#include "vdp.h"
#include "font.h"
#include "spr.h"
#include "hud_data.h"
#include "sprite_data.h"
#include "ghost.h"

#define TX 136                       /* 右側の文字の左端 */
#define GX 8                         /* ゲージの絵の左端(左 8 ドットは MSK で隠れる) */

static u16 shown_score;
static u8  shown_lives, shown_node, shown_lv, shown_armed;

static const char *const node_name[N_STEPS] = {
    "INF", "THRUST", "ECHO", "AEGIS", "INF", "DROP", "SPLIT", "RAY" };
static const u8 node_max[N_STEPS] = { 0, 5, 4, 2, 0, 2, 2, 2 };   /* ★本体の hud.c と同じ(段階の上限) */

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
     項目ごとに文字列を用意し、1 フレームに 2 回(1 回 = 1 行の 6 文字まで)ずつ書き進める。 */
typedef struct { char text[16]; u8 x, y, fg; u8 row, seg; } TextJob;   /* row = 8 で書き終わり。seg = 行の中の書き始めの文字 */
enum { J_SCORE, J_LIVES, J_NODE, NJOB };
/* ★得点は見えない行へ書いてから 1 回で写す。表示の行へ 1 行ずつ書いていた間は、書き替えの途中の数字が別の数字に見えた(5 の上に 2 を
   書く途中は 3 に見え、4500 → 5300 → 5200 と「上がって下がった」ように見えた。指摘) */
#define SCORE_WORK 240               /* ページ 2 の作業行(240..247。HUD の 212..235 の下で、ほかでは使わない) */
static TextJob job[NJOB];

static void job_set(u8 j, u8 x, u8 y, u8 fg, const char *s, u8 width) {
    TextJob *t = &job[j];
    u8 i = 0;
    while (s[i] && i < width) { t->text[i] = s[i]; i++; }
    while (i < width) t->text[i++] = ' ';
    t->text[width] = 0;
    t->x = x; t->y = y; t->fg = fg; t->row = 0; t->seg = 0;
}

/* HUD を描き直す。★一度に描くと 1 フレームに収まらない(場面の切り替えでフレーム落ち)ので、
   消去だけ始めて、残りは hud_update が 1 フレームに 1 段ずつ描く(init_stage)。 */
static u8 init_stage;

void hud_init(void) {
    vdp_hmmv(0, 512 + HUD_VROW - 1, 256, 25, 0x00);         /* ページ 2 の HUD の行(HUD_R23 で上の 1 行も見える) */
    shown_score = 0xFFFF; shown_lives = 0xFF; shown_node = 0xFF; shown_lv = 0xFF; shown_armed = 0xFF;
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

/* ★強化の名前・残機を先に書く(得点を先にしていた間は、得点が数フレームおきに変わり続けると後ろの項目に順番が回らず、
   強化の名前が上の何行かだけ書き替わったまま止まった。指摘「下半分が欠ける」)。1 回に書くのは 6 文字まで: 強化の名前(11 文字)は
   1 行を左右 2 回に分ける(1 行を 1 回で書くと得点の 2 倍かかり、m4 で遅れたフレームが増えた) */
#define SEG_W 6
static void jobs_run(u8 rows) {
    u8 j, lo = rows & HUD_GAUGE_ONLY ? J_NODE : 0;
    rows &= (u8)~HUD_GAUGE_ONLY;
    for (j = NJOB; j-- > lo && rows;) {
        TextJob *t = &job[j];
        while (t->row < 8 && rows) {
            char *p = t->text + t->seg, c = p[SEG_W];
            p[SEG_W] = 0;
            font_text_row(HUD_PAGE, (u8)(t->x + t->seg * 8), t->y, t->fg, 0, p, t->row);
            p[SEG_W] = c;
            if (!t->seg && c) t->seg = SEG_W; else { t->seg = 0; t->row++; }
            if (j == J_SCORE && t->row == 8)                  /* 書き上がったら 1 回で表示の行へ */
            {
                gq_hmmm((u16)(TX + 24), 512u + SCORE_WORK, (u16)(TX + 24), 512u + HUD_ROW0, 40, 8);
                if (g_phase < PH_AIR) gq_pump(0);             /* 待ち行列を回すのは空中戦から(出撃の間はここで出す) */
            }
            rows--; pcmx_service();                           /* ★1 回ごとに PCM の送出口(M7) */
        }
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
    if (init_stage) { if (!(rows & HUD_GAUGE_ONLY)) init_step(); return; }
    /* ★得点は描きかけを描き終えてから次の値を取る(変わるたびに 0 行目から描き直していた間は、ボス戦で毎フレーム得点が
       入ると上の 2 行だけを書き続け、数字が新旧混ざったまま 1 フレームに 2 行の仕事が続いた。8 面の第 2 形態で遅れた) */
    if (g_pl.score != shown_score && job[J_SCORE].row >= 8) {
        shown_score = g_pl.score;
        num_str(buf, g_pl.score, 5);
        job_set(J_SCORE, (u8)(TX + 24), SCORE_WORK, 15, buf, 5);
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
    g_hud_pend = job[J_NODE].row < 8;
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

