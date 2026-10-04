/* enemy.c — 敵・敵弾・アイテム・爆発と出現の台本、当たり判定(指示書 3 / 5.1 の空中戦)。
   座標はどれも中心(固定小数点)。描くときに 8 ドット引いて左上にする。
   ・Tick: 5 機の編隊。右から入り、同じ正弦波の軌道を順にたどる(軌道は x で決まる)。全滅で Quanta。
   ・Tock: Tick と逆位相の編隊。
   ・Notch: 4 機が一列で上(または下)から降り、自機の高さで急旋回して突っ込む。
   ・Spirit: 都市の屋上に張り付く砲台(世界の座標で動く。stage.c が列に合わせて置く)。ときどき自機を狙って撃つ。
   ・敵弾: Tick / Tock がときどき自機を狙って撃つ(2 ドット/フレーム)。
   ・台本は区間ごと(g_wave_set): 空中戦は 1200 フレームで一周、都市はまばらな編隊、それ以外は出さない。 */
#include "game.h"
#include "snd.h"
void pcmx_service(void);   /* snd.c: PCM 方式 B の送出口 */
#include "spr.h"
#include "sprite_data.h"

Enemy  g_enemy[MAX_ENEMY];
Bullet g_eshot[MAX_ESHOT];
Item   g_item[MAX_ITEM];
Fx     g_fx[MAX_FX];
u16    g_frame;

const s8 g_sin64[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
    127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
    0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12 };

/* ---- 編隊 ---- */
#define MAX_FORM 4
static u8 form_left[MAX_FORM];      /* 残り(生きている)機数 */
static u8 form_lost[MAX_FORM];      /* 1 機でも逃した */
static u8 form_next;

static u16 rnd_state = 0xACE1;
u8 rnd8(void) {
    u16 s = rnd_state;
    s ^= (u16)(s << 7); s ^= (u16)(s >> 9); s ^= (u16)(s << 8);
    rnd_state = s;
    return (u8)s;
}

void score_add(u16 v10) { g_pl.score += v10; }

void fx_spawn(s16 x, s16 y) {
    u8 i;
    for (i = 0; i < MAX_FX; i++)
        if (!g_fx[i].t) { g_fx[i].x = x; g_fx[i].y = y; g_fx[i].t = 12; return; }
}

void item_spawn(s16 x, s16 y, u8 kind) {
    u8 i;
    for (i = 0; i < MAX_ITEM; i++) {
        Item *it = &g_item[i];
        if (it->on) continue;
        it->on = 1; it->kind = kind; it->t = 0;
        it->x = x; it->y = y;
        it->vx = (s16)-8 + (s16)(rnd8() & 15) - 8;      /* -1..0 ドット/フレーム */
        it->vy = (s16)(rnd8() & 31) - 16;
        return;
    }
}

void enemy_init(void) {
    u8 i;
    for (i = 0; i < MAX_ENEMY; i++) g_enemy[i].type = E_NONE;
    for (i = 0; i < MAX_ESHOT; i++) g_eshot[i].on = 0;
    for (i = 0; i < MAX_ITEM; i++) g_item[i].on = 0;
    for (i = 0; i < MAX_FX; i++) g_fx[i].t = 0;
    for (i = 0; i < MAX_FORM; i++) form_left[i] = 0;
    g_frame = 0;
    enemy_waves(WAVES_AIR);
}

static Enemy *alloc_enemy(void) {
    u8 i;
    for (i = 0; i < MAX_ENEMY; i++) if (g_enemy[i].type == E_NONE) return &g_enemy[i];
    return 0;
}

static u8 new_form(void) {
    u8 f = form_next;
    form_next = (u8)((form_next + 1) % MAX_FORM);
    form_left[f] = 0;
    form_lost[f] = 0;
    return f;
}

static void spawn_wave(u8 type, s16 y0, s8 dir) {
    u8 i, n = type == E_NOTCH ? 4 : 5;
    u8 f = new_form();
    for (i = 0; i < n; i++) {
        Enemy *e = alloc_enemy();
        if (!e) break;
        e->type = type; e->t = 0; e->hp = 1; e->turned = 0; e->form = f;
        if (type == E_NOTCH) {
            e->x = FX(232) - (s16)i * FX(6);
            e->y = dir > 0 ? FX(-16) - (s16)i * FX(22) : FX(PLAY_H + 16) + (s16)i * FX(22);
            e->vx = FX(-1) + 8; e->vy = dir > 0 ? FX(2) + 8 : FX(-2) - 8;
        } else if (g_wave_mirror) {                      /* 逆走: 左から入る(軌道は左右を返したもの) */
            e->x = FX(-16) - (s16)i * FX(24);
            e->y0 = y0;
            e->vx = FX(1) + 8; e->vy = 0; e->turned = 2;
            e->phase = type == E_TOCK ? 32 : 0;
            e->y = y0;
        } else {
            e->x = FX(272) + (s16)i * FX(24);
            e->y0 = y0;
            e->vx = FX(-1) - 8; e->vy = 0;
            e->phase = type == E_TOCK ? 32 : 0;
            e->y = y0;
        }
        form_left[f]++;
    }
}

/* ---- 台本(区間ごと。LOOP フレームで一周) ---- */
typedef struct { u16 t; u8 type; u8 y0; s8 dir; } Wave;
static const Wave script_air[] = {
    {   60, E_TICK,  50,  0 }, {  200, E_TOCK, 130,  0 }, {  330, E_NOTCH, 0,  1 },
    {  450, E_TICK,  90,  0 }, {  520, E_TOCK,  60,  0 }, {  650, E_NOTCH, 0, -1 },
    {  760, E_TICK, 140,  0 }, {  770, E_TOCK,  40,  0 }, {  900, E_NOTCH, 0,  1 },
    { 1000, E_TICK,  70,  0 }, { 1080, E_NOTCH, 0, -1 },
};
static const Wave script_city[] = {               /* 都市: 砲台が主役。編隊は上空(ビルより上)をまばらに */
    {  240, E_TICK,  30,  0 }, {  700, E_NOTCH, 0,  1 }, {  950, E_TOCK,  24,  0 },
};
#define LOOP  1200
u8 g_wave_set;
u8 g_wave_mirror;
static u16 wave_t;

void enemy_waves(u8 set) { g_wave_set = set; wave_t = 0; }

static void run_script(void) {
    const Wave *w;
    u8 i, n;
    if (g_wave_set == WAVES_AIR) { w = script_air; n = sizeof(script_air) / sizeof(script_air[0]); }
    else if (g_wave_set == WAVES_CITY) { w = script_city; n = sizeof(script_city) / sizeof(script_city[0]); }
    else return;
    if (++wave_t >= LOOP) wave_t = 0;
    for (i = 0; i < n; i++)
        if (w[i].t == wave_t) spawn_wave(w[i].type, FX(w[i].y0), w[i].dir);
}

/* 屋上の砲台: wx = 世界の横位置(近い層のドット)、roof = 屋上の行(都市の s) */
void enemy_spawn_spirit(u16 wx, u8 roof) {
    Enemy *e = alloc_enemy();
    if (!e) return;
    e->type = E_SPIRIT; e->t = rnd8(); e->hp = 3; e->form = 0xFF; e->turned = 0;
    e->y0 = (s16)wx; e->phase = roof;
    e->vx = e->vy = 0;
    e->x = FX((s16)(wx - g_wx)); e->y = FX((s16)roof - g_scam - 6);
}

/* 1 機だけの Notch(5 面の格納庫から上へ飛び出す): 自機の高さで急旋回する */
void enemy_spawn_notch(s16 x, s16 y) {
    Enemy *e = alloc_enemy();
    if (!e) return;
    e->type = E_NOTCH; e->t = 0; e->hp = 1; e->turned = 0; e->form = 0xFF;
    e->x = x; e->y = y; e->vx = FX(-1) + 8; e->vy = FX(-2) - 8;
}

/* 敵弾を 1 発(位置と速さは固定小数点)。空きが無ければ撃たない */
void enemy_bullet(s16 x, s16 y, s16 vx, s16 vy) {
    u8 i;
    if (g_loop) { vx += vx >> 2; vy += vy >> 2; }            /* 2 周目: 弾が 1.25 倍速い */
    for (i = 0; i < MAX_ESHOT; i++) {
        Bullet *b = &g_eshot[i];
        if (b->on) continue;
        b->on = 1;
        b->x = x; b->y = y; b->vx = vx; b->vy = vy;
        return;
    }
}

/* (x, y)(固定小数点)から自機を狙って 2 ドット/フレーム。★長さは max + min × 3/8 で近似する(縦横の大きい方で
   割ると、斜めの弾が 1.4 倍速くなり、砲台の弾の速さがばらついて見えた。M7 の後) */
void enemy_shoot(s16 x, s16 y) {
    s16 dx, dy, ax, ay, m;
    if (!g_pl.alive) return;
    dx = PX(g_pl.x) + 8 - PX(x);
    dy = PX(g_pl.y) + 8 - PX(y);
    ax = dx < 0 ? -dx : dx;
    ay = dy < 0 ? -dy : dy;
    m = ax > ay ? (s16)(ax + ((ay * 3) >> 3)) : (s16)(ay + ((ax * 3) >> 3));
    if (m < 8) return;
    enemy_bullet(x, y, (s16)(dx * 32 / m), (s16)(dy * 32 / m));   /* 固定小数点 32 = 2 ドット */
}

static void enemy_fire(const Enemy *e) { enemy_shoot(e->x, e->y); }

static void kill_enemy(Enemy *e, u8 by_player) {
    u8 f = e->form;
    if (by_player) { fx_spawn(e->x, e->y); score_add(10); sfx(SFX_KILL); }   /* 画面外へ逃げた敵は爆発も得点もなし */
    if (f < MAX_FORM && form_left[f]) {
        form_left[f]--;
        if (!by_player) form_lost[f] = 1;
        if (!form_left[f] && !form_lost[f]) {          /* 編隊を全滅させた */
            item_spawn(e->x, e->y, 0);
            score_add(50);
        }
    }
    e->type = E_NONE;
}

static void move_enemy(Enemy *e) {
    e->t++;
    if (e->type == E_SPIRIT) {                       /* 世界に張り付く: 画面の位置はスクロールから求める */
        e->x = FX((s16)((u16)e->y0 - g_wx));
        e->y = FX((s16)e->phase - g_scam - 6);
        if (e->x < FX(232) && e->x > FX(24) && !(e->t & (g_loop ? 31 : 63))) enemy_fire(e);   /* 2 周目は倍 */
        if (e->x < FX(-16) || e->y > FX(PLAY_H + 16)) kill_enemy(e, 0);
        return;
    }
    if (e->type == E_NOTCH) {
        if (!e->turned) {
            s16 py = g_pl.y + FX(8);
            if ((e->vy > 0 && e->y >= py) || (e->vy < 0 && e->y <= py)) {
                e->turned = 1; e->vx = FX(-4); e->vy = 0;   /* 自機の高さで急旋回 */
            }
        }
        e->x += e->vx; e->y += e->vy;
    } else {
        e->x += e->vx;
        /* 軌道は x で決まる: 後続も同じ場所で同じ高さを通る */
        /* x / 3 と × 5 / 2 は割り算(ライブラリ呼び出し)を避けて掛け算とシフトで */
        {
            u16 px = (u16)(e->turned == 2 ? 256 - PX(e->x) : PX(e->x));   /* 左から来る編隊は左右を返す */
            e->y = e->y0 + (((s16)g_sin64[(u8)((u8)(((px + 32) * 85u) >> 8) + e->phase) & 63] * 5) >> 1);
        }
        if (e->x < FX(240) && e->x > FX(72) && (rnd8() < (u8)(g_loop ? 4 : 2))) enemy_fire(e);
    }
    if (e->x < FX(-16) && e->turned != 2) kill_enemy(e, 0);
    else if (e->x > FX(280) || e->y < FX(-80) || e->y > FX(PLAY_H + 80)) kill_enemy(e, 0);
    else if (e->turned == 2 && e->x < FX(-200)) kill_enemy(e, 0);
}

void enemy_update(void) {
    u8 i, j;
    g_frame++;
    run_script();
    for (i = 0; i < MAX_ENEMY; i++) if (g_enemy[i].type) { move_enemy(&g_enemy[i]); pcmx_service(); }   /* PCM の送出口(敵 1 体ごと) */

    for (i = 0; i < MAX_ESHOT; i++) {
        Bullet *b = &g_eshot[i];
        if (!b->on) continue;
        if (g_grav) {                                    /* 重力: 弾は放物線(中心へはらせん)を描く */
            if (g_grav == GRAV_DIR) { b->vx += g_gx; b->vy += g_gy; }
            else { b->vx += (s16)((g_gcx - PX(b->x)) >> 5); b->vy += (s16)((g_gcy - PX(b->y)) >> 5); }
        }
        b->x += b->vx; b->y += b->vy;
        if (!(i & 3)) pcmx_service();                    /* PCM の送出口(弾 4 発ごと) */
        if (b->x < FX(-8) || b->x > FX(264) || b->y < FX(-8) || b->y > FX(PLAY_H)) { b->on = 0; continue; }
        if (g_pl.alive) {
            s16 dx = PX(b->x) - (PX(g_pl.x) + 8), dy = PX(b->y) - (PX(g_pl.y) + 8);
            if (dx > -4 && dx < 4 && dy > -3 && dy < 3) { b->on = 0; player_hit(); }
        }
    }

    /* 自機のショット × 敵: 画面座標を先に 8 ビットの配列へ変換し、内側は 8 ビットの比較だけにする
       (構造体の固定小数点を毎回変換する二重ループは 1 フレームの 1/3 を使っていた。M1) */
    {
        u8 ex[MAX_ENEMY], ey[MAX_ENEMY], eidx[MAX_ENEMY], ne = 0;
        for (j = 0; j < MAX_ENEMY; j++) {
            const Enemy *e = &g_enemy[j];
            s16 x, y;
            if (!e->type) continue;
            x = PX(e->x); y = PX(e->y);
            if (x < 0 || x > 255 || y < 0 || y > PLAY_H) continue;
            ex[ne] = (u8)x; ey[ne] = (u8)y; eidx[ne] = j; ne++;
        }
        if (ne) for (i = 0; i < MAX_PSHOT; i++) {
            Bullet *b = &g_pshot[i];
            u8 bx, by, k;
            if (!b->on) continue;
            bx = (u8)PX(b->x); by = (u8)PX(b->y);
            for (k = 0; k < ne; k++) {
                Enemy *e;
                if ((u8)(bx - ex[k] + 10) >= 20 || (u8)(by - ey[k] + 8) >= 16) continue;
                e = &g_enemy[eidx[k]];
                if (!e->type) continue;
                if (!b->pierce) b->on = 0;
                if (--e->hp == 0) kill_enemy(e, 1); else sfx(SFX_HIT);
                if (!b->on) break;
            }
        }
    }
    /* 敵 × 自機(体当たり) */
    if (g_pl.alive) {
        for (j = 0; j < MAX_ENEMY; j++) {
            Enemy *e = &g_enemy[j];
            s16 dx, dy;
            if (!e->type) continue;
            dx = PX(e->x) - (PX(g_pl.x) + 8); dy = PX(e->y) - (PX(g_pl.y) + 8);
            if (dx > -8 && dx < 8 && dy > -6 && dy < 6) { kill_enemy(e, 1); player_hit(); break; }
        }
    }
    /* アイテム */
    for (i = 0; i < MAX_ITEM; i++) {
        Item *it = &g_item[i];
        if (!it->on) continue;
        it->t++;
        it->x += it->vx; it->y += it->vy;
        if (g_grav == GRAV_DIR) { it->vx += g_gx; it->vy += g_gy; }   /* 重力: Quanta も落ちる */
        else if (g_grav == GRAV_CENTER) { it->vx += (s16)((g_gcx - PX(it->x)) >> 6); it->vy += (s16)((g_gcy - PX(it->y)) >> 6); }
        else {
            if (it->vy > 0) it->vy -= 1; else if (it->vy < 0) it->vy += 1;   /* 散らばった後は止まる */
            if (it->vx > FX(-1) + 8) it->vx -= 1;
        }
        if (it->x < FX(-16) || it->y < FX(-8) || it->y > FX(PLAY_H)) { it->on = 0; continue; }
        if (g_pl.alive) {
            s16 dx = PX(it->x) - (PX(g_pl.x) + 8), dy = PX(it->y) - (PX(g_pl.y) + 8);
            if (dx > -12 && dx < 12 && dy > -12 && dy < 12) {
                it->on = 0;
                if (it->kind) gauge_shard(); else gauge_quanta();
                score_add(5);
            }
        }
    }
    for (i = 0; i < MAX_FX; i++) if (g_fx[i].t) g_fx[i].t--;
}

void limit_break(void) {
    u8 i;
    boss_damage(10);
    for (i = 0; i < MAX_ENEMY; i++) if (g_enemy[i].type) kill_enemy(&g_enemy[i], 1);
    for (i = 0; i < MAX_ESHOT; i++) if (g_eshot[i].on) { g_eshot[i].on = 0; }
}

void enemy_draw(void) {
    u8 i;
    for (i = 0; i < MAX_ITEM; i++) {
        const Item *it = &g_item[i];
        if (it->on) spr_put(PX(it->x) - 8, PX(it->y) - 8,
                            it->kind ? PAT_SHARD : ((it->t & 8) ? PAT_QUANTA2 : PAT_QUANTA1),
                            G_MISC, it->kind ? CS_SHARD : CS_QUANTA);
    }
    for (i = 0; i < MAX_ESHOT; i++) {
        const Bullet *b = &g_eshot[i];
        if (b->on) spr_put(PX(b->x) - 8, PX(b->y) - 8, PAT_EBUL, G_EBUL, 0);
        if (!(i & 3)) pcmx_service();
    }
    for (i = 0; i < MAX_ENEMY; i++) {
        const Enemy *e = &g_enemy[i];
        u8 pat;
        if (!e->type) continue;
        if (e->type == E_TICK) pat = (e->t & 16) ? PAT_TICK2 : PAT_TICK1;
        else if (e->type == E_TOCK) pat = (e->t & 16) ? PAT_TOCK2 : PAT_TOCK1;
        else if (e->type == E_NOTCH) pat = PAT_NOTCH;
        else pat = PAT_SPIRIT;
        spr_put(PX(e->x) - 8, PX(e->y) - 8, pat, pat == PAT_SPIRIT ? G_ENEMY : G_PAIR, 0);   /* ★編隊の敵は 2 枚重ね(3 色) */
        pcmx_service();
    }
    for (i = 0; i < MAX_FX; i++) {
        const Fx *f = &g_fx[i];
        if (f->t) spr_put(PX(f->x) - 8, PX(f->y) - 8,
                          f->t > 8 ? PAT_EXPL1 : f->t > 4 ? PAT_EXPL2 : PAT_EXPL3, G_MISC, CS_EXPL);
    }
}
