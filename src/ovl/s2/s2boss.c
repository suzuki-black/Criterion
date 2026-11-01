/* s2boss.c — 2 面の中ボス COUNTERWEIGHT とボス KILOGRAM(指示書 5.2)。
   ・COUNTERWEIGHT: 天秤の腕で結ばれた 2 つの錘(スプライト 32x32 × 2)。撃たれた方は軽くなって上がり、重い方が
     下がって自機の側へ振り下ろされる。両方を削らないと逃げ場がなくなる。腕はスプライトの節(回転は sin 表)。
   ・KILOGRAM: 画面の奥から近づく黒い球。大きさごとの絵(半径 7..45)を ROM に並べ、zoom.c が 1 枚ずつ流して BG へ写す。
     着くと、球の上下の帯で背景が曲がる(重力レンズ)。
     周りを小衛星 8 個(スプライト)が回る。一定時間ごとに自機を引き寄せる(重力を球へ)間だけ赤道の亀裂が
     開き(色 13 が明るくなる)、そこだけ傷が入る。
   ★仮の簡略: 指示書の「腕は ROTOR」「最終的に 96x96 の拡大スプライト帯」は、節のスプライトと 96x96 の BG。 */
#include "s2.h"
#include "spr.h"
#include "sprite_data.h"
#include "vdp.h"
#include "rom.h"
#include "ghost.h"
#include "assets.h"

static u8  kind;
static u16 t;

/* ---- 自機のショットとの当たり(8 ビットの表にしてから) ---- */
static u8 shx[MAX_PSHOT], shy[MAX_PSHOT], shi[MAX_PSHOT], nsh;
static void shots_prepare(void) {
    u8 i;
    nsh = 0;
    for (i = 0; i < MAX_PSHOT; i++) {
        const Bullet *b = &g_pshot[i];
        s16 x, y;
        if (!b->on) continue;
        x = PX(b->x); y = PX(b->y);
        if (x < 0 || x > 255 || y < 0 || y > 255) continue;
        shx[nsh] = (u8)x; shy[nsh] = (u8)y; shi[nsh] = i; nsh++;
    }
}
static u8 shot_hit(s16 x, s16 y, u8 rx, u8 ry) {
    u8 k, n = 0, ux, uy;
    if (x < 0 || x > 255 || y < 0 || y > 255) return 0;
    ux = (u8)(x - rx); uy = (u8)(y - ry);
    for (k = 0; k < nsh; k++) {
        Bullet *b;
        if ((u8)(shx[k] - ux) >= (u8)(rx * 2) || (u8)(shy[k] - uy) >= (u8)(ry * 2)) continue;
        b = &g_pshot[shi[k]];
        if (!b->on) continue;
        if (!b->pierce) b->on = 0;
        n++;
    }
    return n;
}
static void touch(s16 x, s16 y, u8 r) {
    s16 dx, dy;
    if (!g_pl.alive) return;
    dx = PX(g_pl.x) + 8 - x; dy = PX(g_pl.y) + 8 - y;
    if (dx > -(s16)r && dx < (s16)r && dy > -(s16)r && dy < (s16)r) player_hit();
}
static void hurt(u8 n) {
    if (g_boss_state != BS_FIGHT) return;
    if (g_boss_hp > n) { g_boss_hp -= n; return; }
    g_boss_hp = 0; g_boss_state = BS_DYING; t = 0;
    score_add(kind == B2_CW ? 300 : 800);
}

/* ---- COUNTERWEIGHT ---- */
#define CW_X  150
#define CW_L  52
static s16 cw_y;                   /* 支点の y */
static u8  hp_l, hp_r;
static s8  ang;                    /* 傾き(64 で一周)。正 = 右が下 */
static s16 lx, ly, rx, ry;         /* 錘の中心 */
static u8  fire_t;

static void cw_pos(void) {
    s16 c = g_sin64[(u8)(ang + 16) & 63], s = g_sin64[(u8)ang & 63];
    lx = (s16)(CW_X - ((c * CW_L) >> 7)); ly = (s16)(cw_y - ((s * CW_L) >> 7) + 20);
    rx = (s16)(CW_X + ((c * CW_L) >> 7)); ry = (s16)(cw_y + ((s * CW_L) >> 7) + 20);
}

static void cw_update(void) {
    if (g_boss_state == BS_ENTER) {
        cw_y += 1;
        if (cw_y >= 36) g_boss_state = BS_FIGHT;
        cw_pos();
        return;
    }
    if (g_boss_state == BS_DYING) {
        if (!(t & 3)) fx_spawn(FX((t & 8 ? lx : rx) - 12 + (rnd8() & 23)), FX((t & 8 ? ly : ry) - 12 + (rnd8() & 23)));
        if (t >= 90) g_boss_state = BS_DONE;
        return;
    }
    {   /* 重い方(体力の多い方)が下がる。少し揺れながら目標の傾きへ */
        s8 target = (s8)(((s16)hp_r - (s16)hp_l) >> 1);
        if (target > 10) target = 10; else if (target < -10) target = -10;
        if (!(t & 3)) { if (ang < target) ang++; else if (ang > target) ang--; }
    }
    cw_pos();
    if (hp_l) { u8 n = shot_hit(lx, ly, 14, 14); while (n--) { if (hp_l) hp_l--; } touch(lx, ly, 12); }
    if (hp_r) { u8 n = shot_hit(rx, ry, 14, 14); while (n--) { if (hp_r) hp_r--; } touch(rx, ry, 12); }
    g_boss_hp = (u8)(hp_l + hp_r);
    if (!g_boss_hp) { g_boss_state = BS_DYING; t = 0; score_add(300); return; }
    if (++fire_t >= 64) {                                     /* ★50 では上の帯に弾が 8〜9 発重なり、敵弾の枠(7)から溢れて消えた(M7 の後) */
        fire_t = 0;
        if (hp_l) enemy_shoot(FX(lx), FX(ly));
        if (hp_r) enemy_shoot(FX(rx), FX(ry));
        if (ly > ry ? hp_l : hp_r) {                          /* 下がった錘は 3 方向 */
            s16 x = ly > ry ? lx : rx, y = ly > ry ? ly : ry;
            enemy_bullet(FX(x), FX(y), -28, 12); enemy_bullet(FX(x), FX(y), -32, 0); enemy_bullet(FX(x), FX(y), -28, -12);
        }
    }
}

static void cw_weight(s16 x, s16 y) {
    spr_put(x - 16, y - 16, PAT_CW_TL, G_ENEMY, 0); spr_put(x, y - 16, PAT_CW_TR, G_ENEMY, 0);
    spr_put(x - 16, y,      PAT_CW_BL, G_ENEMY, 0); spr_put(x, y,      PAT_CW_BR, G_ENEMY, 0);
}

static void cw_draw(void) {
    if (g_boss_state == BS_DYING && (t & 2)) return;
    if (hp_l || g_boss_state != BS_FIGHT) cw_weight(lx, ly);
    if (hp_r || g_boss_state != BS_FIGHT) cw_weight(rx, ry);
    /* 腕の節: 支点と両端の中ほど */
    spr_put((s16)((CW_X + lx) >> 1) - 8, (s16)((cw_y + ly - 20) >> 1) - 8, PAT_CHAIN, G_ENEMY, 0);
    spr_put((s16)((CW_X + rx) >> 1) - 8, (s16)((cw_y + ry - 20) >> 1) - 8, PAT_CHAIN, G_ENEMY, 0);
    spr_put(CW_X - 8, cw_y - 8, PAT_SAT, G_ENEMY, 0);
}

/* ---- KILOGRAM ---- */
#define KG_X    176
#define KG_Y    94
#define NSAT    8
#define KG_R    42                 /* 球の半径(当たり)。★C3: 最後の段は 96x96(描き直した絵) */
#define BIG_N   96                 /* 最後の絵の箱。ページ 2 の x 128..223、64..159 行が作業の場所(2 面では空き。帯の文字は 160 行から) */
static u16 cal_t;                  /* 再校正版の重力の替わり目 */
static u8  kg_shown;               /* 表に絵が出ている(倒したら消す) */
static u8  sat_hp[NSAT], sat_t[NSAT];
static s8  orb_x[64], orb_y[64];   /* 小衛星の軌道(b2_start で作る。★毎フレーム掛け算で出すと 8 個 × 2 回で重かった) */
static u16 open_t;                 /* 吸い込みの周期(0..239 閉じている、240..359 開いて吸い込む) */
u8 b2_lens_tbl_up[50], b2_lens_tbl_dn[48];
static u16 lens_nx;               /* 表を作ったときの横位置(0xFFFF = まだ。b2_start で) */

/* 赤道の亀裂は BG の色 7(★前は色 13 を回していたが、8..15 はスプライトと共有で、閉じるたびに 2 面の敵の体の色が暗くなり、
   衛星が全部まとめて潰れて見えた。遊んでの指摘) */
static void kg_crack(u8 open) { if (open) vdp_set_pal(7, 7, 2, 1); else vdp_set_pal(7, 3, 0, 0); }

/* 重力レンズの表: 球に近いほど大きく横へずらす(0..7 ドット。R#27 を足す向き)。ずらす量 = 7 i² / 529(★毎フレーム割り算で
   作ると 48 回で約 16 ライン。KILOGRAM の間はスプライトの締め切りを毎フレーム過ぎた) */
static const u8 lens_up[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7 };
static void lens_tables(u16 nx) {
    u8 i, r26 = VDP_R26(nx), b = VDP_R27(nx);
    u8 *pu = b2_lens_tbl_up, *pd = b2_lens_tbl_dn;
    for (i = 0; i < 24; i++) {
        u8 a = (u8)(b + lens_up[i]), c = (u8)(b + lens_up[23 - i]);
        *pu++ = r26; *pu++ = a > 7 ? 7 : a;
        *pd++ = r26; *pd++ = c > 7 ? 7 : c;
    }
    *pu++ = r26; *pu = b;                                     /* ★上の帯の最後の行(球の上端)でずれを 0 に戻す(最後の行の 7 ドットの
                                                                 ずれが球の本体の行に残り、戦い始めると球が右へ 3 ドット跳んで縁取りと
                                                                 ずれて見えた。遊んでの指摘「拡大する際の位置がずれる」) */
    lens_nx = nx;
}

static void kg_update(void) {
    u8 k;
    if (g_boss_state == BS_ENTER) {                           /* 近づく(絵は zoom.c が流す) */
        if (g_zm_done) g_boss_state = BS_FIGHT;
        return;
    }
    if (g_boss_state == BS_DYING) {
        if (!(t & 3)) fx_spawn(FX(KG_X - KG_R + (rnd8() & 63) + (rnd8() & 15)), FX(KG_Y - KG_R + (rnd8() & 63) + (rnd8() & 15)));
        g_grav = GRAV_NONE; g_well_r = 56;
        if (t >= 120) g_boss_state = BS_DONE;
        return;
    }
    if (lens_nx != bg_layer_x(2)) lens_tables(bg_layer_x(2));
    if (++open_t >= 360) open_t = 0;
    if (open_t == 240) { kg_crack(1); g_grav = GRAV_CENTER; g_gcx = KG_X; g_gcy = KG_Y; g_well_r = KG_R + 34; }   /* 球の外 34 ドットまで引く */
    if (open_t == 0) { kg_crack(0); g_grav = GRAV_NONE; g_well_r = 56; }
    if (RECAL && open_t < 240 && ++cal_t >= 300) {          /* 再校正版: 重力の向きが 5 秒ごとにでたらめに替わる */
        u8 r = (u8)(rnd8() & 3);
        cal_t = 0; g_grav = GRAV_DIR;
        g_gx = r == 0 ? 1 : r == 1 ? -1 : 0; g_gy = r == 2 ? 1 : r == 3 ? -1 : 0;
    }
    /* 赤道の亀裂(開いている間だけ傷が入る)。球の他の所に当たった弾は吸い込まれる */
    /* ★亀裂の横幅は球と同じにする(弾は 1 フレームに 8 ドット進むので、亀裂を ±26 にすると、自機の位置しだいで弾が
       球の縁(±28)の 2 ドットに入って先に吸い込まれ、亀裂の高さを撃っても当たらなかった。M7 の後) */
    if (open_t >= 240) { u8 n = shot_hit(KG_X, KG_Y, KG_R, 6); g_boss_aim = KG_Y; while (n--) hurt(1); }
    shot_hit(KG_X, KG_Y, KG_R, KG_R);
    touch(KG_X, KG_Y, KG_R - 2);
    /* 小衛星 */
    for (k = 0; k < NSAT; k++) {
        u8 a = (u8)((u8)((t >> 1) + k * 8) & 63);
        s16 x = (s16)(KG_X + orb_x[a]), y = (s16)(KG_Y + orb_y[a]);   /* 球の外を回る */
        if (!sat_hp[k]) { if (++sat_t[k] >= 240) { sat_hp[k] = 2; sat_t[k] = 0; } continue; }
        if (shot_hit(x, y, 7, 7) && --sat_hp[k] == 0) { fx_spawn(FX(x), FX(y)); score_add(5); continue; }
        touch(x, y, 6);
        if ((u8)(t + k * 23) == 0) enemy_shoot(FX(x), FX(y));
    }
}

static void kg_draw(void) {
    u8 k;
    if (g_boss_state != BS_FIGHT) return;
    for (k = 0; k < NSAT; k++) {
        u8 a = (u8)((u8)((t >> 1) + k * 8) & 63);
        if (!sat_hp[k]) continue;
        spr_put((s16)(KG_X - 8 + orb_x[a]), (s16)(KG_Y - 8 + orb_y[a]), PAT_SAT, G_ENEMY, 0);
    }
}

/* ---- 入口 ---- */
void b2_start(u8 k) {
    u8 i;
    kind = k; t = 0; g_boss_state = BS_ENTER; g_boss_dmg = 0;
    if (k == B2_CW) { hp_l = hp_r = 30; g_boss_hp = 60; cw_y = -60; ang = 0; fire_t = 0; }
    else {
        g_boss_hp = 60; kg_shown = 0; zoom_start(ASSET_KG_BANK, ASSET_KG_OFF, 64u * 128u + 64u); open_t = 0; lens_nx = 0xFFFF; cal_t = 0;
        for (i = 0; i < NSAT; i++) { sat_hp[i] = 2; sat_t[i] = 0; }
        for (i = 0; i < 64; i++) { orb_x[i] = (s8)((g_sin64[(u8)(i + 16) & 63] * 66) >> 7); orb_y[i] = (s8)((g_sin64[i] * 52) >> 7); }
        vdp_set_pal(2, 0, 0, 0); vdp_set_pal(3, 1, 1, 1); vdp_set_pal(4, 3, 3, 4); vdp_set_pal(5, 6, 6, 7);
        kg_crack(0);
    }
}

void b2_update(void) {
    g_boss_aim = -1;
    if (g_boss_dmg) { u8 d = g_boss_dmg; g_boss_dmg = 0; hurt(d); }
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE) return;
    t++;
    shots_prepare();
    if (kind == B2_CW) cw_update(); else kg_update();
}

void b2_draw(void) {
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE) return;
    if (kind == B2_CW) cw_draw(); else kg_draw();
}

/* VRAM の窓で: 近づく絵を流して BG へ写す / 倒したら消す */
void b2_vram(void) {
    if (kind != B2_KG || g_boss_state == BS_NONE) { gq_pump(0); return; }
    if (g_boss_state == BS_DONE) {
        if (kg_shown && gq_free() >= 8) { ring_cmd(0, 0, KG_X - BIG_N / 2, KG_Y - BIG_N / 2, BIG_N, BIG_N, 1, 0x11); kg_shown = 0; }
    } else if (g_boss_state == BS_ENTER && s2_far_zero && !g_scroll_speed && gq_free() >= 8 && zoom_step()) {
        /* ★遠景のずれが 0 に戻り、スクロールが止まってから(s2.c)。止まる前に写した最初の段は世界と一緒に流れ、試験 02 では
           左に出て次の段で跳んだ(遊んでの指摘「2 回目の登場で初期位置がずれて拡大が不自然」) */
        ring_cmd(128, 512u + 64u, (s16)(KG_X - g_zm_w), (s16)(KG_Y - (g_zm_h >> 1)), (u8)(g_zm_w * 2), g_zm_h, 0, 0);
        kg_shown = 1;
    }
    gq_pump(0);
}

u8 b2_lens(s16 *top, s16 *bot) {
    if (kind != B2_KG || g_boss_state != BS_FIGHT || lens_nx == 0xFFFF) return 0;
    *top = KG_Y - BIG_N / 2 - 24; *bot = KG_Y + BIG_N / 2 + 1;
    return 1;
}
