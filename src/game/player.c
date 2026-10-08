/* player.c — 自機 Infinitas とショット(指示書 2.1 / 2.3)。
   ・8 方向移動。速さは THRUST の段階で上がる(1.5 + 0.5 × 段階 ドット/フレーム)。
   ・A 押しっぱなしで連射。SPLIT 1 = 前方 3WAY、2 = 5WAY 扇。RAY 1 = 貫通弾、2 = 太さ倍(上下 2 本)。
   ・当たり判定は中心 4x2 ドット(見た目より小さく)。
   ・被弾: 残機 -1、強化はすべて失い、その場に Quanta を 3 個散らす。復活後 2 秒は無敵(点滅)。
   ・ECHO / AEGIS / DROP は第 2 バンクの weapon.c(ここからは weap_* を呼ぶだけ)。 */
#include "game.h"
#include "snd.h"
#include "input.h"
#include "spr.h"
#include "sprite_data.h"

Player g_pl;
Bullet g_pshot[MAX_PSHOT];
u8  g_grav;
s16 g_gx, g_gy, g_gcx, g_gcy;
s16 g_pl_ymin, g_pl_ymax = PLAY_H - 16;
s16 g_pl_dy;
static s16 gvx, gvy;               /* 重力でついた速さ(固定小数点) */
static u8  grav_key, grav_t;        /* 重力の種類と向き(替わったら立ち上げ直す)、替わってからのフレーム */
u8 g_pl_boost;                     /* 上下の速さに足す(1/16 ドット/フレーム)。3 面の高速の区間で面が書く */
u8 g_well_r = 56;                  /* 渦(GRAV_CENTER)が引く範囲(ドット)。2 面の KILOGRAM は球が大きいので広げる */

#define GV_MAX FX(1) + 8            /* 重力の速さの上限(1.5 ドット/フレーム) */

#define START_X  FX(32)
#define START_Y  FX(84)
#define FIRE_CD  6

void player_init(void) {
    u8 i;
    g_pl.x = START_X; g_pl.y = START_Y;
    g_pl.alive = 1; g_pl.invul = 120; g_pl.fire_cd = 0; g_pl.dead_t = 0;
    g_pl.lives = 3; g_pl.score = 0;
    gvx = gvy = 0; g_grav = GRAV_NONE; g_pl_boost = 0; g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16; g_pl_dy = 0;
    for (i = 0; i < MAX_PSHOT; i++) g_pshot[i].on = 0;
    weap_init();
}

u8 g_fired;
u8 g_shot_at;                      /* 次に空きを探す弾の枠(撃つフレームの初めに 0。weapon.c の pl_fire_from) */
u8 g_drops;                        /* 生きている DROP の爆雷の数(weapon.c) */
#define WEAP_ON() (g_gauge.thrust | g_gauge.echo | g_gauge.aegis | g_gauge.drop | g_drops)   /* 第 2 バンクの仕事があるか */
/* 分身・防壁・残像はスプライトの段の最後に(main.c の player_weap_draw。溢れたら真っ先に消える側) */
void player_weap_draw(void) { if (WEAP_ON()) weap_draw(); }
Terrain g_ter;

void player_update(void) {
    u8 i;
    /* ショット(画面外で消える)。★処理落ちのフレームは弾も止める: 当たり判定(敵・ボス)は処理落ちのフレームに
       走らないので、動かすと 2 フレームぶん進んで小さな的をすり抜けた(M7。音で処理落ちが増えて目立った) */
    for (i = 0; i < PS_SHOTS; i++) {                          /* 爆雷(PS_SHOTS から)は weapon.c が進める */
        if (g_slow) break;
        Bullet *b = &g_pshot[i];
        if (!b->on) continue;
        b->x += b->vx; b->y += b->vy;
        if (b->x > FX(256) || b->y < FX(-8) || b->y > FX(PLAY_H)) b->on = 0;
    }
    if (!g_pl.alive) {
        if (g_pl.dead_t && --g_pl.dead_t == 0 && g_pl.lives) {
            g_pl.alive = 1; g_pl.invul = 120;
            g_pl.x = START_X; g_pl.y = START_Y;
            weap_init();
        }
        if (g_drops) weap_update();                           /* やられている間も爆雷は進める */
        return;
    }
    {
        s16 sp = FX(1) + 8 + (s16)g_gauge.thrust * 8;   /* 1.5 + 0.5 × THRUST */
        if (g_in & IN_UP)    g_pl.y -= sp + g_pl_boost;
        if (g_in & IN_DOWN)  g_pl.y += sp + g_pl_boost;
        if (g_in & IN_LEFT)  g_pl.x -= sp;
        if (g_in & IN_RIGHT) g_pl.x += sp;
        /* 重力: 放っておくと流される。重力に逆らう向きに入れると、その向きの流れは止まる */
        if (g_grav) {
            s16 ax = g_gx, ay = g_gy;
            u8 key = (u8)(g_grav | ((g_gx & 3) << 2) | ((g_gy & 3) << 4));
            if (key != grav_key) { grav_key = key; grav_t = 0; }
            if (grav_t < 255) grav_t++;
            if (g_grav == GRAV_CENTER) {
                /* ★渦: 近づいたときだけ一定の強さで引く(画面の 1 点へ距離に比例して引くと、振り子のように回り続けて
                   向きが毎フレーム変わり、人には追えなかった。ゴーファーの野望の重力面にならう。M7 の後) */
                s16 dx = (s16)(g_gcx - PX(g_pl.x) - 8), dy = (s16)(g_gcy - PX(g_pl.y) - 8);
                ax = ay = 0;
                if (dx > -(s16)g_well_r && dx < (s16)g_well_r && dy > -(s16)g_well_r && dy < (s16)g_well_r) {
                    ax = dx > 6 ? 1 : dx < -6 ? -1 : 0;
                    ay = dy > 6 ? 1 : dy < -6 ? -1 : 0;
                }
            }
            /* ★立ち上げ: 替わってから 40 フレームは掛けず、4 フレームに 1 回 → 2 フレームに 1 回 → 毎フレーム
               (空中戦の直後にいきなり引かれるのはきつかった。予告は面が出す。M7 の後) */
            if (!ax) gvx = gvx > 0 ? gvx - 1 : gvx < 0 ? gvx + 1 : 0;   /* 引かれない向きの流れは止まっていく */
            if (!ay) gvy = gvy > 0 ? gvy - 1 : gvy < 0 ? gvy + 1 : 0;
            if (!(grav_t < 40 || (grav_t < 100 && (grav_t & 3)) || (grav_t < 160 && (grav_t & 1)))) { gvx += ax; gvy += ay; }
            if (gvx > GV_MAX) gvx = GV_MAX; else if (gvx < -(GV_MAX)) gvx = -(GV_MAX);
            if (gvy > GV_MAX) gvy = GV_MAX; else if (gvy < -(GV_MAX)) gvy = -(GV_MAX);
            if ((g_in & IN_UP) && gvy > 0) gvy = 0;
            if ((g_in & IN_DOWN) && gvy < 0) gvy = 0;
            if ((g_in & IN_LEFT) && gvx > 0) gvx = 0;
            if ((g_in & IN_RIGHT) && gvx < 0) gvx = 0;
            g_pl.x += gvx; g_pl.y += gvy;
        } else gvx = gvy = 0;
        if (g_pl.x < FX(8))  { g_pl.x = FX(8); gvx = 0; }
        if (g_pl.x > FX(232)) { g_pl.x = FX(232); gvx = 0; }
        /* 地形(天井・床)に当たって止まる。★はみ出しが 4 ドットを超える = 限りの側が動いた(区間の節目で通り道が狭まった)
           ときは、1 フレームに 1 ドットずつ押す(一度に押し戻すと、自機が勝手に跳んだように見えた。M7 の後) */
        if (g_pl.y < FX(g_pl_ymin)) { g_pl.y = (s16)(FX(g_pl_ymin) - g_pl.y > FX(4) ? g_pl.y + FX(1) : FX(g_pl_ymin)); gvy = 0; }
        if (g_pl.y > FX(g_pl_ymax)) { g_pl.y = (s16)(g_pl.y - FX(g_pl_ymax) > FX(4) ? g_pl.y - FX(1) : FX(g_pl_ymax)); gvy = 0; }
    }
    if (!g_slow) {
        if (g_pl.fire_cd) g_pl.fire_cd--;
        g_fired = 0; g_shot_at = 0;
        if ((g_in & IN_A) && !g_pl.fire_cd) { pl_fire_from(g_pl.x, g_pl.y); g_fired = 1; sfx(SFX_SHOT); g_pl.fire_cd = FIRE_CD; }
    }
    if (g_in_edge & IN_B) gauge_confirm();
    if (g_pl.invul) g_pl.invul--;
    if (WEAP_ON()) weap_update();                             /* ★第 2 バンクは仕事のあるときだけ */
}

void player_hit(void) {
    u8 i;
    if (!g_pl.alive || g_pl.invul) return;
    g_pl.alive = 0;
    g_pl.dead_t = 90;
    sfx(SFX_DIE);
    if (g_pl.lives) g_pl.lives--;
    fx_spawn(g_pl.x + FX(8), g_pl.y + FX(8));                       /* 爆発・アイテムの座標は中心 */
    for (i = 0; i < 3; i++) item_spawn(g_pl.x + FX(8), g_pl.y + FX(8), 0);
    gauge_lose();
}

void player_draw(void) {
    u8 i;
    if (g_pl.alive && !(g_pl.invul & 4)) {
        s16 y = (s16)(PX(g_pl.y) + g_pl_dy);
        spr_put(PX(g_pl.x), y, PAT_SHIP_A, G_SHIP_A, 0);
        spr_put(PX(g_pl.x), y, PAT_SHIP_B, G_SHIP_B, 0);           /* OR 合成(CC)= 直前の枠と重ねる */
    }
    if (g_drops) for (i = PS_SHOTS; i < MAX_PSHOT; i++) {     /* ★爆雷を先に(弾が G_SHOT の 6 枠を超えると、後から置いた爆雷が毎回溢れた) */
        const Bullet *b = &g_pshot[i];
        if (b->on) spr_put(PX(b->x) - 8, PX(b->y) - 8, PAT_DROP, G_SHOT, 0);
    }
    for (i = 0; i < PS_SHOTS; i++) {
        const Bullet *b = &g_pshot[i];
        if (b->on) spr_put(PX(b->x) - 2, PX(b->y) - 8, b->pierce ? PAT_RAY : PAT_SHOT, G_SHOT, 0);
    }
}
