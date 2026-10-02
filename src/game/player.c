/* player.c — 自機 Infinitas とショット(指示書 2.1 / 2.3)。
   ・8 方向移動。速さは THRUST の段階で上がる(1.5 + 0.5 × 段階 ドット/フレーム)。
   ・A 押しっぱなしで連射。SPLIT 1 = 前方 3WAY、2 = 5WAY 扇。RAY 1 = 貫通弾、2 = 太さ倍(上下 2 本)。
   ・当たり判定は中心 4x2 ドット(見た目より小さく)。
   ・被弾: 残機 -1、強化はすべて失い、その場に Quanta を 3 個散らす。復活後 2 秒は無敵(点滅)。
   ★ECHO / AEGIS / DROP の効果は以降の段階(段階の数だけはゲージで増える)。 */
#include "game.h"
#include "input.h"
#include "spr.h"
#include "sprite_data.h"

Player g_pl;
Bullet g_pshot[MAX_PSHOT];

#define START_X  FX(32)
#define START_Y  FX(84)
#define FIRE_CD  6

void player_init(void) {
    u8 i;
    g_pl.x = START_X; g_pl.y = START_Y;
    g_pl.alive = 1; g_pl.invul = 120; g_pl.fire_cd = 0; g_pl.dead_t = 0;
    g_pl.lives = 3; g_pl.score = 0;
    for (i = 0; i < MAX_PSHOT; i++) g_pshot[i].on = 0;
}

static void shoot(s16 vy, u8 pierce, s16 dy) {
    u8 i;
    for (i = 0; i < MAX_PSHOT; i++) {
        Bullet *b = &g_pshot[i];
        if (b->on) continue;
        b->on = 1; b->pierce = pierce;
        b->x = g_pl.x + FX(12); b->y = g_pl.y + FX(8) + dy;   /* 弾の座標は中心 */
        b->vx = FX(8); b->vy = vy;
        return;
    }
}

static void fire(void) {
    if (g_gauge.ray) {
        shoot(0, 1, 0);
        if (g_gauge.ray >= 2) { shoot(0, 1, FX(-4)); shoot(0, 1, FX(4)); }
    } else {
        shoot(0, 0, 0);
        if (g_gauge.split >= 1) { shoot(FX(-1) - 8, 0, 0); shoot(FX(1) + 8, 0, 0); }
        if (g_gauge.split >= 2) { shoot(FX(-3), 0, 0); shoot(FX(3), 0, 0); }
    }
}

void player_update(void) {
    u8 i;
    /* ショット(画面外で消える) */
    for (i = 0; i < MAX_PSHOT; i++) {
        Bullet *b = &g_pshot[i];
        if (!b->on) continue;
        b->x += b->vx; b->y += b->vy;
        if (b->x > FX(256) || b->y < FX(-8) || b->y > FX(PLAY_H)) b->on = 0;
    }
    if (!g_pl.alive) {
        if (g_pl.dead_t && --g_pl.dead_t == 0 && g_pl.lives) {
            g_pl.alive = 1; g_pl.invul = 120;
            g_pl.x = START_X; g_pl.y = START_Y;
        }
        return;
    }
    {
        s16 sp = FX(1) + 8 + (s16)g_gauge.thrust * 8;   /* 1.5 + 0.5 × THRUST */
        if (g_in & IN_UP)    g_pl.y -= sp;
        if (g_in & IN_DOWN)  g_pl.y += sp;
        if (g_in & IN_LEFT)  g_pl.x -= sp;
        if (g_in & IN_RIGHT) g_pl.x += sp;
        if (g_pl.x < FX(8))  g_pl.x = FX(8);
        if (g_pl.x > FX(232)) g_pl.x = FX(232);
        if (g_pl.y < FX(0))  g_pl.y = FX(0);
        if (g_pl.y > FX(PLAY_H - 16)) g_pl.y = FX(PLAY_H - 16);
    }
    if (g_pl.fire_cd) g_pl.fire_cd--;
    if ((g_in & IN_A) && !g_pl.fire_cd) { fire(); g_pl.fire_cd = FIRE_CD; }
    if (g_in_edge & IN_B) gauge_confirm();
    if (g_pl.invul) g_pl.invul--;
}

void player_hit(void) {
    u8 i;
    if (!g_pl.alive || g_pl.invul) return;
    g_pl.alive = 0;
    g_pl.dead_t = 90;
    if (g_pl.lives) g_pl.lives--;
    fx_spawn(g_pl.x + FX(8), g_pl.y + FX(8));                       /* 爆発・アイテムの座標は中心 */
    for (i = 0; i < 3; i++) item_spawn(g_pl.x + FX(8), g_pl.y + FX(8), 0);
    gauge_lose();
}

void player_draw(void) {
    u8 i;
    if (g_pl.alive && !(g_pl.invul & 4)) {
        spr_put(PX(g_pl.x), PX(g_pl.y), PAT_SHIP_A, G_SHIP_A, 0);
        spr_put(PX(g_pl.x), PX(g_pl.y), PAT_SHIP_B, G_SHIP_B, 0);  /* OR 合成(CC)= 直前の枠と重ねる */
    }
    for (i = 0; i < MAX_PSHOT; i++) {
        const Bullet *b = &g_pshot[i];
        if (b->on) spr_put(PX(b->x) - 2, PX(b->y) - 8, b->pierce ? PAT_RAY : PAT_SHOT, G_SHOT, 0);
    }
}
