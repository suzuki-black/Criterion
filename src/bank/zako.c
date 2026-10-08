/* zako.c — 面ごとの雑魚と台本(指示書 3 章。第 2 バンク)。
   enemy.c の敵のプール(g_enemy)を共に使い、型が E_BK から先のものをここで動かして描く。弾との当たり・体当たり・
   編隊の全滅の Quanta・得点は enemy.c のまま(当たりは中心 (x, y) のまわり 20x16)。
   ・Caliper(ノギス): 床を歩き、脚を開いて上下から挟む 2 発。      1・5 面
   ・Calibrator(校正ハッチ): 床に据わり、小型機を続けて出す。         1・5・8 面
   ・Dial(円盤目盛り): 回りながら漂い、止まった瞬間に 3 方向へ撃つ。   2・6・7 面
   ・Plummet(下げ振り): 天井から糸で降り、自機の真上で落ちる。         2・6 面
   ・Protractor(分度器): 扇状に 5 発。                                  3・6・7 面
   ・Compass(コンパス): 片脚を軸に半円を描いて進む。                   4・7 面
   ・Abacus(そろばん玉): 8 つの玉の列。全部壊すと Quanta(編隊)。       4・7・8 面
   ・Sextant(六分儀): 大型。自機の角度を測る印を出してから速い弾で狙う。 5・8 面
   ・Hourglass Mite(砂時計): 漂い、撃たれると砂を撒く。                  2・6・7 面
   ・Scanner(測定線): 画面を左から右へ縦断する線。当たると強化を 1 段奪われる。 7 面(3 面は面の演出)
   台本は面(g_ovl)と区間の種類(g_wave_set)で選ぶ。面の頭の空中戦(WAVES_SKY)・試験・逆走は enemy.c のまま。 */
#include "game.h"
#include "spr.h"
#include "snd.h"
#include "ovl.h"
#include "vdp.h"
#include "sprite_data.h"
#include "bank_int.h"

/* ---- 台本 ---- */
enum { K_TICK, K_TOCK, K_NOTCH_DN, K_NOTCH_UP, K_CALIPER, K_HATCH, K_DIAL, K_PLUMMET, K_PROTR, K_COMPASS, K_ABACUS,
       K_SEXTANT, K_SMITE, K_SCANNER };
typedef struct { u16 t; u8 kind; u8 y; } ZWave;

static const ZWave sc_s1[] = {   /* 1 面の都市: 編隊は上空にまばら、地上を Caliper とハッチ */
    { 120, K_CALIPER, 0 }, { 240, K_TICK, 30 }, { 420, K_HATCH, 0 }, { 600, K_CALIPER, 0 }, { 700, K_NOTCH_DN, 0 },
    { 850, K_HATCH, 0 }, { 950, K_TOCK, 24 }, { 1050, K_CALIPER, 0 },
};
static const ZWave sc_s2[] = {   /* 2 面の重力の区間 */
    { 60, K_DIAL, 60 }, { 200, K_SMITE, 100 }, { 320, K_PLUMMET, 0 }, { 420, K_TICK, 50 }, { 520, K_DIAL, 130 },
    { 640, K_PLUMMET, 0 }, { 760, K_SMITE, 60 }, { 880, K_TOCK, 120 }, { 1000, K_DIAL, 90 },
};
static const ZWave sc_s3[] = {   /* 3 面の回廊 */
    { 60, K_TICK, 50 }, { 200, K_PROTR, 90 }, { 330, K_NOTCH_DN, 0 }, { 450, K_PROTR, 50 }, { 600, K_TOCK, 130 },
    { 720, K_PROTR, 120 }, { 900, K_NOTCH_UP, 0 }, { 1000, K_TICK, 70 },
};
static const ZWave sc_s4[] = {   /* 4 面の大気・雲海・陽炎 */
    { 60, K_COMPASS, 60 }, { 220, K_ABACUS, 90 }, { 400, K_TICK, 50 }, { 520, K_COMPASS, 120 }, { 700, K_ABACUS, 60 },
    { 880, K_TOCK, 130 }, { 1000, K_COMPASS, 90 },
};
static const ZWave sc_s5[] = {   /* 5 面の艦首・甲板 */
    { 100, K_CALIPER, 0 }, { 300, K_HATCH, 0 }, { 480, K_SEXTANT, 60 }, { 650, K_NOTCH_DN, 0 }, { 800, K_CALIPER, 0 },
    { 950, K_HATCH, 0 },
};
static const ZWave sc_s6[] = {   /* 6 面の外壁・影 */
    { 60, K_DIAL, 60 }, { 220, K_PLUMMET, 0 }, { 380, K_SMITE, 90 }, { 520, K_PROTR, 110 }, { 680, K_DIAL, 120 },
    { 840, K_PLUMMET, 0 }, { 1000, K_PROTR, 60 },
};
static const ZWave sc_s6d[] = {  /* 6 面の文字盤 */
    { 100, K_PROTR, 70 }, { 400, K_DIAL, 110 }, { 700, K_PROTR, 120 }, { 1000, K_DIAL, 60 },
};
static const ZWave sc_s7[] = {   /* 7 面の空中戦: 計測器の雑魚を一通り */
    { 40, K_TICK, 40 }, { 150, K_COMPASS, 90 }, { 260, K_DIAL, 60 }, { 380, K_SCANNER, 0 }, { 480, K_ABACUS, 120 },
    { 600, K_SMITE, 70 }, { 720, K_PROTR, 100 }, { 850, K_NOTCH_DN, 0 }, { 950, K_TOCK, 130 },
};
static const ZWave sc_s8[] = {   /* 8 面の空中戦 */
    { 60, K_ABACUS, 60 }, { 250, K_SEXTANT, 90 }, { 420, K_TICK, 120 }, { 560, K_ABACUS, 130 }, { 760, K_SEXTANT, 50 },
    { 900, K_NOTCH_DN, 0 }, { 1000, K_TOCK, 40 },
};

static u8 rnd_lo(void) { return rnd8(); }

/* 床(天井)に据わる雑魚: 画面の右端の外の床。無ければ出さない */
static Enemy *spawn_on(u8 type, u8 hp, u8 up) {
    s16 f = ter_y(250, up);
    Enemy *e;
    if (f == TER_NONE) return 0;
    if (!(e = enemy_alloc())) return 0;
    e->type = type; e->t = 0; e->hp = hp; e->form = 0xFF; e->turned = 0; e->phase = 0;
    e->y0 = (s16)(g_wx + 264u);                               /* 世界の横位置 */
    e->x = FX(264); e->y = FX(f); e->vx = e->vy = 0;
    g_zako_n++;
    return e;
}

static Enemy *spawn_air(u8 type, u8 hp, u8 y) {
    Enemy *e = enemy_alloc();
    if (!e) return 0;
    e->type = type; e->t = 0; e->hp = hp; e->form = 0xFF; e->turned = 0; e->phase = rnd_lo();
    e->x = FX(268); e->y = FX(y); e->y0 = y; e->vx = e->vy = 0;
    g_zako_n++;
    return e;
}

static void spawn(u8 kind, u8 y) {
    Enemy *e;
    switch (kind) {
    case K_TICK: enemy_wave(E_TICK, FX(y), 0); break;
    case K_TOCK: enemy_wave(E_TOCK, FX(y), 0); break;
    case K_NOTCH_DN: enemy_wave(E_NOTCH, 0, 1); break;
    case K_NOTCH_UP: enemy_wave(E_NOTCH, 0, -1); break;
    case K_CALIPER: spawn_on(E_CALIPER, 4, 0); break;
    case K_HATCH: spawn_on(E_HATCH, 6, 0); break;
    case K_PLUMMET:
        if ((e = spawn_on(E_PLUMMET, 2, 1)) != 0) e->vy = 0;  /* 天井から */
        else if ((e = spawn_air(E_PLUMMET, 2, 0)) != 0) e->y0 = (s16)(g_wx + 268u);   /* 天井が無ければ画面の上から */
        break;
    case K_DIAL: spawn_air(E_DIAL, 3, y); break;
    case K_PROTR: spawn_air(E_PROTR, 3, y); break;
    case K_COMPASS:
        if ((e = spawn_air(E_COMPASS, 3, y)) != 0) { e->y0 = 248; e->phase = y; e->turned = 0; }   /* 軸 (y0, phase)、角 turned */
        break;
    case K_ABACUS: {
        u8 i, f = enemy_new_form();
        for (i = 0; i < 8; i++) {
            if (!(e = spawn_air(E_ABACUS, 1, y))) break;
            e->x = FX(268) + (s16)i * FX(12); e->form = f; g_form_left[f]++;
        }
        break;
    }
    case K_SEXTANT: spawn_air(E_SEXTANT, 14, y); break;
    case K_SMITE: spawn_air(E_SMITE, 2, y); break;
    case K_SCANNER:
        if ((e = spawn_air(E_SCANNER, 255, 0)) != 0) {
            u8 i;
            e->y0 = 0; e->y = FX(-200);                       /* 当たりは画面の外(弾では消せない) */
            vdp_write_addr17(1, (u16)(0xF800u + PAT_SCAN * 32u)); for (i = 0; i < 32; i++) VDP_DAT = i < 16 ? 0x01 : 0x80;
        }
        break;
    }
}

u8 zako_script(void) {
    const ZWave *w;
    u8 n, i;
    if (g_calib || g_wave_mirror) return 0;
    if (g_wave_set == WAVES_CITY) {
        if (g_ovl == OVL_S1) { w = sc_s1; n = sizeof sc_s1 / sizeof sc_s1[0]; }
        else if (g_ovl == OVL_S5) { w = sc_s5; n = sizeof sc_s5 / sizeof sc_s5[0]; }
        else if (g_ovl == OVL_S6) { w = sc_s6d; n = sizeof sc_s6d / sizeof sc_s6d[0]; }
        else return 0;
    } else if (g_wave_set == WAVES_AIR) {
        switch (g_ovl) {
        case OVL_S2: w = sc_s2; n = sizeof sc_s2 / sizeof sc_s2[0]; break;
        case OVL_S3: w = sc_s3; n = sizeof sc_s3 / sizeof sc_s3[0]; break;
        case OVL_S4: w = sc_s4; n = sizeof sc_s4 / sizeof sc_s4[0]; break;
        case OVL_S6: w = sc_s6; n = sizeof sc_s6 / sizeof sc_s6[0]; break;
        case OVL_S7: w = sc_s7; n = sizeof sc_s7 / sizeof sc_s7[0]; break;
        case OVL_S8: w = sc_s8; n = sizeof sc_s8 / sizeof sc_s8[0]; break;
        default: return 0;
        }
    } else return 0;
    for (i = 0; i < n; i++, w++) if (w->t == g_wave_t) spawn(w->kind, w->y);
    return 1;
}

/* ---- 動き ---- */
#define FIRE_MASK(m) ((g_loop) ? ((m) >> 1) : (m))       /* 2 周目は倍撃つ */

/* (x, y) から自機へ向かう角に、上下へ ofs ずらした向き(固定小数点の速さ sp)で 1 発 */
static void shoot_ofs(s16 x, s16 y, s16 ofs, s16 sp) {
    s16 dx = (s16)(PX(g_pl.x) + 8 - PX(x)), dy = (s16)(PX(g_pl.y) + 8 + ofs - PX(y)), ax, ay, m;
    if (!g_pl.alive) return;
    ax = dx < 0 ? -dx : dx; ay = dy < 0 ? -dy : dy;
    m = ax > ay ? (s16)(ax + ((ay * 3) >> 3)) : (s16)(ay + ((ax * 3) >> 3));
    if (m < 8) return;
    enemy_bullet(x, y, (s16)(dx * sp / m), (s16)(dy * sp / m));
}

static u8 anchored(Enemy *e) {   /* 世界に据わる: 画面の x を求め、左へ出たら 0 */
    s16 sx = (s16)((u16)e->y0 - g_wx);
    e->x = FX(sx);
    return sx > -24;
}

static void m_caliper(Enemy *e) {
    s16 sx, f;
    e->y0 -= (e->t & 1);                                      /* 2 フレームに 1 ドット、前へ歩く */
    if (!anchored(e)) { enemy_kill(e, 0); return; }
    sx = PX(e->x);
    f = ter_y(sx, 0);
    if (f != TER_NONE) e->y = FX(f - 16);                     /* 中心は脚の上端(体は上の 16 行、脚は下の 16 行) */
    if ((e->t & FIRE_MASK(127)) == 64 && sx > 24 && sx < 240) {   /* 脚を開いて上下から挟む */
        shoot_ofs(e->x, e->y, -24, 28); shoot_ofs(e->x, e->y, 24, 28);
    }
}

static void m_hatch(Enemy *e) {
    s16 sx, f;
    if (!anchored(e)) { enemy_kill(e, 0); return; }
    sx = PX(e->x);
    f = ter_y(sx, 0);
    if (f != TER_NONE) e->y = FX(f - 8);
    if ((e->t & FIRE_MASK(63)) == 32 && sx > 16 && sx < 240) {   /* 小型機を出す */
        Enemy *c = enemy_alloc();
        if (c) {
            c->type = E_CRAFT; c->t = 0; c->hp = 1; c->form = 0xFF; c->turned = 0;
            c->x = e->x; c->y = e->y - FX(8); c->vx = 0; c->vy = -FX(2);
            g_zako_n++;
        }
    }
}

static void m_craft(Enemy *e) {
    if (!e->turned && (e->y <= g_pl.y + FX(8) || e->t > 60)) { e->turned = 1; e->vx = -FX(3); e->vy = 0; }
    e->x += e->vx; e->y += e->vy;
}

static void m_dial(Enemy *e) {
    u8 k = (u8)(e->t % 90u);
    if (k < 60) { e->x -= 12; e->y = FX(e->y0) + ((s16)g_sin64[(u8)(e->t + e->phase) & 63] << 2); }   /* 回りながら漂う */
    else if (k == 60 && PX(e->x) < 240) {                     /* 止まった瞬間に 3 方向 */
        shoot_ofs(e->x, e->y, 0, 32); shoot_ofs(e->x, e->y, -32, 32); shoot_ofs(e->x, e->y, 32, 32);
    }
}

static void m_plummet(Enemy *e) {
    s16 sx, c;
    if (!anchored(e)) { enemy_kill(e, 0); return; }
    sx = PX(e->x);
    c = ter_y(sx, 1);
    if (c == TER_NONE) c = 0;
    e->phase = (u8)(c < 0 ? 0 : c);                           /* 糸の上端(画面の y) */
    if (e->turned == 0) {                                     /* 降りる */
        if (PX(e->y) < c + 40) e->y += FX(1);
        else e->y = FX(c + 40);
        if (g_pl.alive && (u16)(PX(g_pl.x) + 8 - sx + 10) < 20 && PX(g_pl.y) > PX(e->y)) { e->turned = 1; e->vy = FX(1); }
    } else {                                                  /* 落ちる */
        if (e->vy < FX(5)) e->vy += 6;
        e->y += e->vy;
        if (PX(e->y) > PLAY_H + 16) enemy_kill(e, 0);
    }
}

static void m_protr(Enemy *e) {
    e->x -= 8;
    if ((e->t & FIRE_MASK(127)) == 80 && PX(e->x) < 236) {    /* 扇状に 5 発 */
        s8 k;
        for (k = -2; k <= 2; k++) shoot_ofs(e->x, e->y, (s16)k * 28, 28);
    }
}

static void m_compass(Enemy *e) {
    u8 a = e->turned;
    /* 軸 (y0, phase) のまわりを上回りに半周(角 0 = 右 → 128 = 左)。半周したら脚を替える: 軸を 2R 左へ */
    e->x = FX((s16)e->y0 + (((s16)g_sin64[(u8)((a >> 2) + 16) & 63] * 20) >> 7));
    e->y = FX((s16)e->phase - (((s16)g_sin64[(a >> 2) & 63] * 20) >> 7));
    a += 4;
    if (a >= 128) { a = 0; e->y0 -= 40; }
    e->turned = a;
    if ((e->t & FIRE_MASK(127)) == 100 && PX(e->x) < 240) shoot_ofs(e->x, e->y, 0, 32);
}

static void m_abacus(Enemy *e) {
    u16 px;
    e->x -= 20;
    px = (u16)PX(e->x);
    e->y = FX(e->y0) + ((s16)g_sin64[(u8)(((px + 32) * 170u) >> 8) & 63] * 3 >> 1);   /* 数珠つなぎ: 後ろの玉も同じ所を通る */
}

static void m_sextant(Enemy *e) {
    if (e->turned == 0) {                                     /* 入る */
        e->x -= FX(1);
        if (e->x <= FX(200)) { e->turned = 1; e->t = 0; }
    } else if (e->turned <= 3) {                              /* 測る(40 フレーム印を出す)→ 撃つ。3 回 */
        if (e->t == 1) { e->vx = (s16)(PX(g_pl.x) + 8); e->vy = (s16)(PX(g_pl.y) + 8); }   /* 測った位置 */
        if (e->t == 40) {
            s16 dx = (s16)(e->vx - PX(e->x)), dy = (s16)(e->vy - PX(e->y)), ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            s16 m = ax > ay ? (s16)(ax + ((ay * 3) >> 3)) : (s16)(ay + ((ax * 3) >> 3));
            if (m >= 8) enemy_bullet(e->x - FX(12), e->y, (s16)(dx * 64 / m), (s16)(dy * 64 / m));   /* 4 ドット/フレームの精密狙撃 */
            sfx(SFX_TICK);
        }
        if (e->t >= 70) { e->turned++; e->t = 0; }
    } else {                                                  /* 去る */
        e->x += FX(1);
        if (e->x > FX(290)) enemy_kill(e, 0);
    }
}

static void m_smite(Enemy *e) {
    e->x -= 10;
    e->y = FX(e->y0) + ((s16)g_sin64[(u8)((e->t >> 1) + e->phase) & 63] * 3 >> 2);
}

static void m_scanner(Enemy *e) {
    s16 bx;
    e->y0 += 2;
    bx = e->y0;
    if (bx > 256) { enemy_kill(e, 0); return; }
    if (!e->turned && g_pl.alive && (u16)(PX(g_pl.x) + 8 - bx + 3) < 6) { e->turned = 1; gauge_steal(); }   /* 測られて強化を 1 段奪われる */
}

void zako_update(void) {
    u8 i, n = 0;
    Enemy *e = g_enemy;
    for (i = 0; i < MAX_ENEMY; i++, e++) {
        if (e->type < E_BK) continue;
        e->t++;
        switch (e->type) {
        case E_CALIPER: m_caliper(e); break;
        case E_HATCH: m_hatch(e); break;
        case E_CRAFT: m_craft(e); break;
        case E_DIAL: m_dial(e); break;
        case E_PLUMMET: m_plummet(e); break;
        case E_PROTR: m_protr(e); break;
        case E_COMPASS: m_compass(e); break;
        case E_ABACUS: m_abacus(e); break;
        case E_SEXTANT: m_sextant(e); break;
        case E_SMITE: m_smite(e); break;
        case E_SCANNER: m_scanner(e); break;
        }
        if (e->type >= E_BK && e->type != E_SCANNER) {         /* 画面の外へ出たら消す(得点なし) */
            if (e->x < FX(-24) || e->x > FX(300) || e->y < FX(-48) || e->y > FX(PLAY_H + 32)) enemy_kill(e, 0);
        }
        if (e->type >= E_BK) n++;
    }
    g_zako_n = n;
}

/* 自機に倒された: 砂時計は砂を撒く(重みで下へ流れる弾) */
void zako_killed(Enemy *e) {
    if (e->type == E_SMITE) {
        s8 k;
        for (k = -2; k <= 1; k++) enemy_bullet(e->x, e->y, (s16)k * 6, FX(1) + (s16)(k & 1) * 6);
    }
}

/* ---- 描く(1 枚の敵: G_ENEMY) ---- */
static const u8 pat_one[] = { 0, PAT_HATCH, PAT_CRAFT, 0, 0, PAT_PROTR, PAT_COMPASS, PAT_ABACUS, 0, PAT_SMITE, 0 };   /* E_BK から。0 = 下で描く */
void zako_draw(void) {
    u8 i, ty;
    const Enemy *e = g_enemy;
    for (i = 0; i < MAX_ENEMY; i++, e++) {
        s16 x, y;
        u8 p;
        if (e->type < E_BK) continue;
        x = PX(e->x) - 8; y = PX(e->y) - 8;
        p = pat_one[e->type - E_BK];
        if (p) spr_put(x, y, p, G_ENEMY, 0);
        else if (e->type == E_CALIPER) {
            spr_put(x, y - 8, PAT_CALIPER_B, G_ENEMY, 0);
            spr_put(x, y + 8, (e->t & 16) ? PAT_CALIPER_L2 : PAT_CALIPER_L1, G_ENEMY, 0);
        } else if (e->type == E_DIAL) {
            spr_put(x, y, (u8)(PAT_DIAL0 + ((e->t % 90u) < 60 ? (e->t >> 2) & 3 : 0)), G_ENEMY, 0);
        } else if (e->type == E_PLUMMET) {
            for (ty = e->phase; (s16)ty < y - 2; ty += 16) spr_put(x, ty, PAT_THREAD, G_ENEMY, 0);   /* 糸 */
            spr_put(x, y, PAT_PLUMMET, G_ENEMY, 0);
        } else if (e->type == E_SEXTANT) {
            spr_put(x - 8, y - 8, PAT_SEXT_TL, G_ENEMY, 0); spr_put(x + 8, y - 8, PAT_SEXT_TR, G_ENEMY, 0);
            spr_put(x - 8, y + 8, PAT_SEXT_BL, G_ENEMY, 0); spr_put(x + 8, y + 8, PAT_SEXT_BR, G_ENEMY, 0);
            if (e->turned >= 1 && e->turned <= 3 && e->t > 1 && e->t < 40 && (e->t & 4)) {   /* 測っている印(自機へ向かう点) */
                s16 dx = (s16)(e->vx - PX(e->x)), dy = (s16)(e->vy - PX(e->y));
                spr_put((s16)(PX(e->x) + (dx >> 1) - 8), (s16)(PX(e->y) + (dy >> 1) - 8), PAT_CURSOR, G_MISC, CS_CURSOR);
            }
        } else if (e->type == E_SCANNER) {
            for (ty = 0; ty < 176; ty += 16) spr_put((s16)(e->y0 - 8), ty, PAT_SCAN, G_EBUL, 0);
        }
    }
}
