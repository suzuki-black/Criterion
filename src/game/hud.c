/* hud.c — ∞ゲージ(指示書 2.3 / 2.4)の変数と操作。HUD 帯の表示(hud_init / hud_update / hud_sprites)は第 3 バンクの
   src/bank3/hud.c(常駐の空きのため)。gauge_confirm は第 2 バンクの infx_start を呼ぶので、第 3 バンクへは移せない(入れ子)。 */
#include "game.h"
#include "snd.h"

Gauge g_gauge;

#define PATH_STEPS 8                 /* カーソルの光点が隣のノードへ動く段数(hud_data.h と同じ。表を取り込まないため数で持つ) */

static const u8 node_max[N_STEPS] = { 0, 5, 4, 2, 0, 2, 2, 2 };   /* ★第 3 バンクの hud.c と同じ(段階の上限) */

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
