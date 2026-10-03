/* s4boss.c — 4 面の中ボス VERNIER、巨大キャラ RULER、ボス STRATUM(指示書 5.4)。すべて BG(表示リング)に描き、
   動きは帯ごとの横位置(R#26/R#27)だけで作る。描くのは登場のときに一度(GHOST の待ち行列で数フレームに分けて)。
   ・VERNIER: 上下 2 枚の目盛り板(層 3 と層 8)。反対向きにすべる。上の目盛りは 16 ドットおき、下は 15 ドット
     おき(副尺)なので、ずれ Δ に応じて i - 15m = Δ の目盛りが一致する。一致したらそこから極太のビーム
     (予告 30 フレーム → 30 フレーム)。板の真ん中の核 2 つを壊す。
   ・RULER: 目盛りの刻まれた体節 8 つ。体節 k は層 L_k に 1 つずつ(体節 = 帯)。帯ごとの横位置を正弦でずらすと
     体がうねる。流れで画面を左へ抜けたら、見えないところ(リングの画面外)で消して、別の層の並びで描き直す
     (4 回通る)。体は壊せない(当たると被弾)。奇数の体節の砲台(スプライト)だけ壊せる。
   ・STRATUM: 顔(128x144)を 6 本の帯(24 行ずつ)に描き、帯の横位置をスロットのリールのように動かす。揃うと
     無敵で目から弾。ずれた瞬間、どれか 1 本の帯に核が出る(そのときだけ撃てる)。 */
#include "s4.h"
#include "spr.h"
#include "sprite_data.h"
#include "vdp.h"
#include "rom.h"
#include "ghost.h"
#include "assets.h"
#include "s4_data.h"
#include "sys.h"

static u8  kind;
static u16 t;

/* ---- 自機のショットとの当たり ---- */
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
static u8 shot_hit(s16 x, s16 y, u8 r) {
    u8 k, n = 0, ux, uy;
    if (x < 0 || x > 255 || y < 0 || y > 255) return 0;
    ux = (u8)(x - r); uy = (u8)(y - r);
    for (k = 0; k < nsh; k++) {
        Bullet *b;
        if ((u8)(shx[k] - ux) >= (u8)(r * 2) || (u8)(shy[k] - uy) >= (u8)(r * 2)) continue;
        b = &g_pshot[shi[k]];
        if (!b->on) continue;
        if (!b->pierce) b->on = 0;
        n++;
    }
    return n;
}
/* 自機の中心が矩形 (x, y, w, h) の中なら被弾 */
static void touch_rect(s16 x, s16 y, u8 w, u8 h) {
    s16 cx, cy;
    if (!g_pl.alive) return;
    cx = PX(g_pl.x) + 8; cy = PX(g_pl.y) + 8;
    if (cx >= x && cx < x + w && cy >= y && cy < y + h) player_hit();
}

/* ======== VERNIER ======== */
#define VT_L   3                   /* 上の板の層 */
#define VB_L   8                   /* 下の板の層 */
#define VT_S   51                  /* 上の板の行(s)。層 3 = 47..62 */
#define VB_S   113                 /* 下の板の行。層 8 = 110..124 */
#define V_W    224
#define V_H    10
static u16 vt_rx, vb_rx;           /* 板の左端のリングの x */
static s16 vt_sx, vb_sx;           /* 板の左端の画面の x */
static u8  v_drawn;                /* 描いた命令の数(0..V_CMDS) */
#define V_CMDS 4                   /* 板は部品(ページ 2)から左右 112 ずつ写す */
static u8  core_hp[2];
static u8  beam_t;                 /* 0 = なし、1..30 予告、31..60 ビーム */
static s16 beam_x;
static u8  beam_cool;
static u8  v_ang;

/* 部品(ページ 2 の x = 128..)からリングへ */
static void parts_copy(u8 px, u8 py, u16 rx, s16 s, u8 w, u8 h) {
    ring_cmd((u16)(128u + px), (u16)(512u + py), (s16)(rx - g_wx), (s16)(s - g_scam), w, h, 0, 0);
}
static void vern_cmd(u8 k) {       /* 板の k 番目の描画命令(上の左・右、下の左・右) */
    u16 rx = (u16)(((k & 2) ? vb_rx : vt_rx) + ((k & 1) ? 112u : 0u));
    parts_copy(0, (u8)(24 + (k & 1) * 10 + (k & 2) * 10), rx, (k & 2) ? VB_S : VT_S, 112, V_H);
}

static void vern_start(void) {
    /* 上の板は画面の右の外から、下の板は左の外から(リングの見えないところに描く) */
    vt_rx = (u16)((s4_band_x(VT_L) + 272) & 0x1FE);
    vb_rx = (u16)((s4_band_x(VB_L) - 240) & 0x1FE);
    vt_sx = 272; vb_sx = -240;
    s4_band_hold(VT_L, s4_band_x(VT_L)); s4_band_hold(VB_L, s4_band_x(VB_L));
    v_drawn = 0; core_hp[0] = core_hp[1] = 20; g_boss_hp = 40; beam_t = 0; beam_cool = 90; v_ang = 0;
}

/* 目盛りの一致: 上 i(0..13)と下 j(0..14)で vt_sx + 16i ≒ vb_sx + 15j。j = i + m とすると i = Δ + 15m */
static s16 vern_match(void) {
    s16 d0 = (s16)(vb_sx - vt_sx), d;
    s8 m;
    for (d = d0 - 1; d <= d0 + 1; d++)
        for (m = -14; m <= 14; m++) {
            s16 i = (s16)(d + 15 * m), j = (s16)(i + m), x;
            if (i < 0 || i > 13 || j < 0 || j > 14) continue;
            x = (s16)(vt_sx + 8 + i * 16);
            if (x >= 16 && x < 232) return x;
        }
    return -1;
}

static void vern_update(void) {
    u8 k;
    if (g_boss_state == BS_ENTER) {                           /* 描き終えたら、両側からすべり込む */
        if (v_drawn < V_CMDS) return;
        if (vt_sx > 16) vt_sx -= 4; if (vb_sx < 16) vb_sx += 4;
        if (vt_sx <= 16 && vb_sx >= 16) { vt_sx = 16; vb_sx = 16; g_boss_state = BS_FIGHT; }
    } else if (g_boss_state == BS_DYING) {
        if (!(t & 3)) { fx_spawn(FX(vt_sx + (s16)(((u16)rnd8() * 7u) >> 3)), FX(VT_S + 4)); fx_spawn(FX(vb_sx + (s16)(((u16)rnd8() * 7u) >> 3)), FX(VB_S + 4)); }
        if (t == 90) s4_ring_restore(vt_rx, VT_S, V_W, V_H);  /* 板を雲に戻す(1 フレーム 1 枚) */
        if (t == 92) s4_ring_restore(vb_rx, VB_S, V_W, V_H);
        if (t >= 96 && gq_free() == GQ_N) {
            s4_band_hold(VT_L, 0xFFFF); s4_band_hold(VB_L, 0xFFFF);
            g_boss_state = BS_DONE;
            return;                                           /* 帯は流れに戻した(下で押さえ直さない) */
        }
    } else {
        /* 反対向きにすべる(振れ幅 40、だんだん速く) */
        s16 w;
        v_ang += (u8)(1 + (t > 600) + (t > 1200));
        w = (s16)((g_sin64[(u8)(v_ang >> 2) & 63] * 40) >> 7);
        vt_sx = (s16)(16 + w); vb_sx = (s16)(16 - w);
        /* 核 */
        for (k = 0; k < 2; k++) {
            s16 cx = (s16)((k ? vb_sx : vt_sx) + V_W / 2), cy = k ? VB_S + 5 : VT_S + 4;
            if (!core_hp[k]) continue;
            g_boss_aim = (s16)(cy - g_scam);
            if (shot_hit(cx, cy, 9) && --core_hp[k] == 0) { fx_spawn(FX(cx), FX(cy)); score_add(300); }
            if (!(t & 63) && k == (u8)((t >> 6) & 1)) enemy_shoot(FX(cx), FX(cy));
        }
        g_boss_hp = (u8)(core_hp[0] + core_hp[1]);
        if (!g_boss_hp) { g_boss_state = BS_DYING; t = 0; beam_t = 0; score_add(2000); return; }
        /* 目盛りが一致したらビーム */
        if (beam_t) { if (++beam_t > 60) { beam_t = 0; beam_cool = 50; } }
        else if (beam_cool) beam_cool--;
        else { s16 x = vern_match(); if (x >= 0) { beam_x = x; beam_t = 1; } }
        if (beam_t > 30) touch_rect(beam_x - 7, 0, 16, PLAY_H);
        touch_rect(vt_sx, VT_S - g_scam, V_W, V_H);           /* 板そのものも固い */
        touch_rect(vb_sx, VB_S - g_scam, V_W, V_H);
    }
    s4_band_hold(VT_L, (u16)(vt_rx - vt_sx)); s4_band_hold(VB_L, (u16)(vb_rx - vb_sx));
}

static void vern_draw(void) {
    u8 k;
    if (g_boss_state == BS_FIGHT) {
        for (k = 0; k < 2; k++)
            if (core_hp[k]) spr_put((s16)((k ? vb_sx : vt_sx) + V_W / 2 - 8), (s16)((k ? VB_S + 5 : VT_S + 4) - 8 - g_scam), PAT_CORE, G_ENEMY, 0);
        if (beam_t && (beam_t > 30 || (beam_t & 2))) {
            u8 pat = beam_t > 30 ? PAT_BEAM : PAT_SCAN;
            for (k = 0; k < 12; k++) spr_put(beam_x - 8, (s16)(k * 16), pat, G_ENEMY, 0);
        }
    }
}

/* ======== RULER ======== */
#define NSEG   8
#define SEG_W  28
#define SEG_H  12
#define SEG_DX 24
#define R_PASS 4
static u16 r_base;                 /* 流れの左端(リングの x、帯の共通の部分) */
static u16 r_base0;                /* この回の体を置いたときの r_base(進み = r_base - r_base0) */
static u16 r_q;                    /* その端数(1/4 ドット) */
static s16 r_off[S4_NL];           /* 帯ごとの元の位置の差(始めに取る。絵が跳ねない) */
static u16 seg_rx[NSEG];
static u8  seg_l[NSEG];
static u8  tur_hp[NSEG];
static u8  r_pass, r_phase;        /* 何回目 / 0 = 描く、1 = 泳ぐ、2 = 消す(r_cmd = 消した節の数) */
static u8  r_cmd;                  /* 描いた命令の数 */
static u8  r_wait;                 /* 始めの描き込みを待つ(VERNIER の爆発のスプライトと重なって遅れた) */
static u8  r_amp;                  /* うねりの振れ幅: 正弦(±127)を右へずらす数(7 = ±1 → 3 = ±15) */
#define R_CMDS NSEG                /* 体節は部品から 1 回ずつ写す */
static const u8 r_path[R_PASS][NSEG] = {
    { 2, 3, 4, 5, 6, 7, 8, 9 },    /* 右上から左下へ */
    { 9, 8, 7, 6, 5, 4, 3, 2 },
    { 1, 2, 3, 4, 4, 3, 2, 1 },    /* 弧 */
    { 10, 9, 8, 7, 7, 8, 9, 10 } };

static u16 r_layer_x(u8 l) {       /* ★掛け算はライブラリ呼び出し(12 帯 × 毎フレーム)なので、振れ幅はシフトで */
    s8 w = (s8)(g_sin64[(u8)((t >> 1) + l * 6) & 63] >> r_amp);
    return (u16)(r_base + r_off[l] + w);
}
static s16 seg_sx(u8 k) { return s4_ring_sx(seg_rx[k], seg_l[k]); }
static s16 seg_s(u8 k) { return (s16)(s4_layer[seg_l[k]] + 3); }

static void ruler_place(void) {    /* 次の回: 画面の右の外(リングの見えないところ)へ */
    u8 k;
    for (k = 0; k < NSEG; k++) {
        seg_l[k] = r_path[r_pass][k];
        seg_rx[k] = (u16)((r_base + r_off[seg_l[k]] + 272 + k * SEG_DX) & 0x1FE);
        tur_hp[k] = (k & 1) ? 3 : 0;
    }
    r_base0 = r_base; r_cmd = 0; r_phase = 0;
}

static void ruler_cmd(u8 k) { parts_copy(k ? 0 : 32, 0, seg_rx[k], seg_s(k), SEG_W, SEG_H); }   /* 0 = 頭 */

static void ruler_start(void) {
    u8 l;
    r_base = s4_band_x(0); r_q = 0; r_amp = 7; r_pass = 0; r_wait = 30;
    for (l = 0; l < S4_NL; l++) { r_off[l] = (s16)(s4_band_x(l) - r_base); s4_band_hold(l, s4_band_x(l)); }
    ruler_place();
    g_boss_hp = 0;
}

static void ruler_update(void) {
    u8 k, l;
    if (r_wait) r_wait--;
    r_q += 6;                                                 /* 大河の流れ 1.5 ドット/フレーム */
    r_base += r_q >> 2; r_q &= 3;
    if (r_amp > 3 && !(t & 31)) r_amp--;
    for (l = 0; l < S4_NL; l++) s4_band_hold(l, r_layer_x(l));
    if (g_boss_state == BS_ENTER) g_boss_state = BS_FIGHT;
    if (r_phase == 2) {                                       /* 消す(1 フレーム 2 節。まとめて積むと遅れた)→ 次の回 */
        if (gq_free() < 12) return;
        for (k = 0; k < 2 && r_cmd < NSEG; k++, r_cmd++) s4_ring_restore(seg_rx[r_cmd], seg_s(r_cmd), SEG_W, SEG_H);
        if (r_cmd < NSEG) return;
        if (++r_pass >= R_PASS) {
            for (l = 0; l < S4_NL; l++) s4_band_hold(l, 0xFFFF);
            g_boss_state = BS_DONE; return;
        }
        ruler_place();
        return;
    }
    if (r_phase == 1) {
        u16 prog = (u16)(r_base - r_base0);                   /* ★画面の x はリングの 512 で回るので、進みで判定する */
        if (prog > 272 + (NSEG - 1) * SEG_DX + SEG_W + 32) { r_phase = 2; r_cmd = 0; return; }   /* 左へ抜けた: 消す */
        /* 体(当たると被弾)と砲台 */
        for (k = 0; k < NSEG; k++) {
            s16 sx = seg_sx(k), sy = (s16)(seg_s(k) - g_scam), real = (s16)(272 + k * SEG_DX - (s16)prog);
            if (real < -SEG_W - 16 || real > 260 || sx < -SEG_W || sx > 255) continue;
            touch_rect(sx, sy, SEG_W, SEG_H);
            if (tur_hp[k]) {
                s16 tx = sx + 14, ty = sy - 6;
                if (shot_hit(tx, ty, 7) && --tur_hp[k] == 0) { fx_spawn(FX(tx), FX(ty)); score_add(100); }
                else if (((t + k * 23) & 127) == 0 && tx > 16 && tx < 240) enemy_shoot(FX(tx), FX(ty));
            }
        }
    }
}

static void ruler_draw(void) {
    u8 k;
    u16 prog = (u16)(r_base - r_base0);
    if (r_phase != 1) return;
    for (k = 0; k < NSEG; k++) {
        s16 sx, real = (s16)(272 + k * SEG_DX - (s16)prog);
        if (!tur_hp[k] || real < -32 || real > 260) continue;
        sx = seg_sx(k);
        if (sx < -16 || sx > 255) continue;
        spr_put(sx + 6, (s16)(seg_s(k) - g_scam - 14), PAT_TURRET, G_ENEMY, 0);
    }
}

/* ======== STRATUM ======== */
#define FY     22                  /* 顔の上端の行(s)。帯は 24 行ずつ 6 本 */
#define F_W    128
#define F_H    144
#define NREEL  6
static u16 f_rx;                   /* 顔の左端のリングの x */
static s16 reel[NREEL];            /* 帯ごとのずれ(ドット。0 = 揃う) */
static s16 reel_to[NREEL];
static u8  f_load, f_off;          /* 顔(0..143 行)と部品(144.. = 部品の 0..63 行)をページ 2 へ読む */
static u8  f_cmd;                  /* 顔をリングへ写した数(8 行ずつ 18 回) */
static u8  st_phase;               /* 0 = 揃っている、1 = ずれて核が出ている */
static u16 st_t;
static u8  core_band, core_hits;
static u16 f_top, f_bot;           /* 上下の雲の帯の横位置 */

void b4_load_face(void) { f_load = 1; f_off = 0; }

static void stratum_start(void) {
    u8 i;
    f_rx = (u16)((s4_band_x(5) + 320) & 0x1FE);               /* 顔は見えないところに描いて、リールで寄せる */
    for (i = 0; i < NREEL; i++) { reel[i] = (s16)(256 + i * 40); reel_to[i] = 0; }
    f_cmd = 0; st_phase = 0; st_t = 0; core_hits = 0;
    f_top = s4_band_x(0); f_bot = s4_band_x(11);
    g_boss_hp = 36;
}

static void reels_move(u8 fast) {
    u8 i;
    for (i = 0; i < NREEL; i++) {
        s16 d = (s16)(reel_to[i] - reel[i]);
        s16 step = (s16)(fast ? (d >> 3) : (d >> 4));
        if (step == 0 && d) step = d > 0 ? 1 : -1;
        if (fast && i * 10 > (s16)st_t) continue;             /* 揃うときはリールを 1 本ずつ止める */
        reel[i] += step;
    }
}

static u8 reels_still(void) {
    u8 i;
    for (i = 0; i < NREEL; i++) if (reel[i] != reel_to[i]) return 0;
    return 1;
}

static void stratum_update(void) {
    u8 i;
    st_t++;
    f_top = s4_band_x(0); f_bot = s4_band_x(11);
    if (g_boss_state == BS_ENTER) {
        if (f_cmd < F_H / 8) return;                          /* 顔を写し終えてから */
        reels_move(1);
        if (reels_still()) { g_boss_state = BS_FIGHT; st_phase = 0; st_t = 0; }
        return;
    }
    if (g_boss_state == BS_DYING) {
        for (i = 0; i < NREEL; i++) reel[i] += (s16)((i & 1) ? 3 : -3);   /* 帯がばらばらに流れ去る */
        if (!(t & 3)) fx_spawn(FX(64 + (rnd8() & 127)), FX(FY + (s16)(((u16)rnd8() * 9u) >> 4)));
        if (t >= 150 && t < 150 + F_H / 8) s4_ring_restore(f_rx, (s16)(FY + (t - 150) * 8), F_W, 8);   /* 顔を雲に戻す(8 行ずつ) */
        if (t >= 150 + F_H / 8 && gq_free() == GQ_N) g_boss_state = BS_DONE;
        return;
    }
    if (st_phase == 0) {                                      /* 揃っている: 無敵、目から弾 */
        reels_move(1);
        if (!(st_t & 15) && st_t > 30) {
            s16 ex = (s16)(64 - reel[1] + ((st_t & 16) ? 38 : 89)), ey = (s16)(FY + 40 - g_scam);
            enemy_bullet(FX(ex), FX(ey), -FX(2), (s16)(((s16)(rnd8() & 63) - 32) << 1));
            enemy_shoot(FX(ex), FX(ey));
        }
        if (st_t >= 240) {                                    /* ずれる: どれか 1 本に核 */
            st_phase = 1; st_t = 0;
            core_band = (u8)(((u16)rnd8() * NREEL) >> 8); core_hits = 0;
            for (i = 0; i < NREEL; i++) {
                s16 d = (s16)(40 + (rnd8() & 63));
                reel_to[i] = (rnd8() & 1) ? d : (s16)-d;
            }
        }
    } else {                                                  /* ずれている: 核だけ撃てる */
        s16 cx = (s16)(64 - reel[core_band] + 64), cy = (s16)(FY + core_band * 24 + 12 - g_scam);
        reels_move(0);
        if (reels_still()) g_boss_aim = cy;
        if (reels_still() && shot_hit(cx, cy, 11)) {
            core_hits++;
            if (g_boss_hp) g_boss_hp--;
            if (!g_boss_hp) { g_boss_state = BS_DYING; t = 0; score_add(5000); return; }
        }
        if (core_hits >= 12 || st_t >= 300) {
            st_phase = 0; st_t = 0;
            for (i = 0; i < NREEL; i++) reel_to[i] = 0;
        }
        if (!(st_t & 31)) enemy_shoot(FX(cx), FX(cy));
    }
}

static void stratum_draw(void) {
    if (g_boss_state == BS_FIGHT && st_phase == 1 && reels_still())
        spr_put((s16)(64 - reel[core_band] + 64 - 8), (s16)(FY + core_band * 24 + 4 - g_scam), PAT_CORE, G_ENEMY, 0);
}

/* ---- 入口 ---- */
void b4_start(u8 k) {
    kind = k; t = 0; g_boss_state = BS_ENTER; g_boss_dmg = 0;
    if (k == B4_VERNIER) vern_start();
    else if (k == B4_RULER) ruler_start();
    else stratum_start();
}

void b4_update(void) {
    if (g_boss_dmg) {                                         /* LIMIT BREAK */
        u8 d = g_boss_dmg; g_boss_dmg = 0;
        if (g_boss_state == BS_FIGHT) {
            if (kind == B4_VERNIER) { u8 k; for (k = 0; k < 2; k++) core_hp[k] = core_hp[k] > d ? (u8)(core_hp[k] - d) : (core_hp[k] ? 1 : 0); }
            else if (kind == B4_STRATUM && st_phase == 1) g_boss_hp = g_boss_hp > d ? (u8)(g_boss_hp - d) : 1;
        }
    }
    g_boss_aim = -1;
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE) return;
    t++;
    shots_prepare();
    if (kind == B4_VERNIER) vern_update();
    else if (kind == B4_RULER) ruler_update();
    else stratum_update();
}

void b4_draw(void) {
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE) return;
    if (kind == B4_VERNIER) vern_draw();
    else if (kind == B4_RULER) ruler_draw();
    else stratum_draw();
}

void b4_vram(void) {
    extern u16 g_frame_t0;
    u8 n = 0;
    if (f_load && (u16)(sys_timer() - g_frame_t0) < 150u * 16u) {
        /* 顔をページ 2 の 0..143 行・0..127 ドットへ(2 行/フレーム、重いフレームは次へ。RULER の描画と重なると
           VDP のコマンドの間の CPU の書き込みが遅れて 240 ラインかかった) */
        u8 r;
        for (r = 0; r < 2 && f_off < F_H + 64; r++, f_off++)
            if (f_off < F_H) rom_to_vram(ASSET_FACE_BANK, (u16)(ASSET_FACE_OFF + f_off * 64u), 1, (u16)(f_off * 128u), 64);
            else rom_to_vram(ASSET_S4_PARTS_BANK, (u16)(ASSET_S4_PARTS_OFF + (f_off - F_H) * 64u), 1, (u16)((f_off - F_H) * 128u + 64u), 64);   /* 部品は x = 128.. */
        if (f_off >= F_H + 64) f_load = 0;
    }
    if (g_boss_state != BS_NONE && g_boss_state != BS_DONE) {
        if (kind == B4_VERNIER)
            while (v_drawn < V_CMDS && gq_free() > 4 && n++ < 8) vern_cmd(v_drawn++);   /* 1 フレーム 8 個まで */
        else if (kind == B4_RULER && r_phase == 0) {
            while (!r_wait && r_cmd < R_CMDS && gq_free() > 4 && n++ < 4) ruler_cmd(r_cmd++);
            if (r_cmd >= R_CMDS) r_phase = 1;
        } else if (kind == B4_STRATUM && !f_load && f_cmd < F_H / 8 && gq_free() > 8) {
            for (n = 0; n < 2 && f_cmd < F_H / 8; n++, f_cmd++)   /* 8 行ずつ(1KB ≒ 6ms) */
                ring_cmd(0, (u16)(512u + f_cmd * 8u), (s16)(f_rx - g_wx), (s16)(FY + f_cmd * 8 - g_scam), F_W, 8, 0, 0);
        }
    }
    gq_pump(0);
}

/* STRATUM の間の帯: 上の雲 / 顔の 6 本 / 下の雲 */
u8 b4_bands(s16 sc) {
    static const u8 sky_top[3] = { 2, 0, 3 }, sky_face[3] = { 3, 1, 2 }, sky_bot[3] = { 6, 3, 1 };
    u8 i;
    if (kind != B4_STRATUM || g_boss_state == BS_NONE || g_boss_state == BS_DONE) return 0;
    stage_ev_pal((s16)(0 - sc), sky_top);
    stage_ev_scroll((s16)(0 - sc), f_top);
    for (i = 0; i < NREEL; i++) {
        s16 l = (s16)(FY + i * 24 - sc);
        if (i == 0) stage_ev_pal(l, sky_face);
        stage_ev_scroll(l, (u16)(f_rx - 64 + reel[i]));
    }
    stage_ev_pal((s16)(FY + F_H - sc), sky_bot);
    stage_ev_scroll((s16)(FY + F_H - sc), f_bot);
    return 1;
}
