/* s6boss.c — 6 面の歯車・砂粒虫・数字の砲台、中ボス HOURGLASS、ボス GNOMON(指示書 5.6)。
   ・歯車と砂時計は ROTOR で回したコマを面の入りで作ってページ 3(68 行から下: 6 面の短冊は 0..67 行しか使わない)
     とページ 2 に置き、HMMM で表示リングへ写す(歯車 8 コマ = 45°(歯 8 枚)で一周、30fps。砂時計 6 コマ = 0..150°)。
   ・砂粒虫: 外壁の棚のスプライト。砂粒(落ちる弾)をこぼす。影の中では敵の体の色(12)が暗くなり、目(弾の色 8 の
     スプライト)だけが光る。
   ・HOURGLASS: 64x64(★指示書は 96。ROTOR の写し先が 64x64 まで)。3 秒ごとに 180° 回り、回っている間だけ括れ
     (弱点)が出る。回り終えると上の砂が括れからこぼれる(落ちる弾)。
   ・数字の砲台: 文字盤の台座の上の I..XII(フォントで BG に描く。壊すと空で塗る)。
   ・GNOMON: 画面のリングがページ 0 にそろう位置で止め、根元(BG)を軸に長さ 120 の針を VDP の LINE で描く
     (3 本の平行線。毎 2 フレーム、前の線を空の色で消してから描く)。触れると被弾。一周ごとに速く、3 周目から逆回り。 */
#include "s6.h"
#include "spr.h"
#include "sprite_data.h"
#include "vdp.h"
#include "rom.h"
#include "ghost.h"
#include "rotor.h"
#include "font.h"
#include "assets.h"
#include "s6_data.h"
#include "sys.h"

#define PAT_MITE 44
#define PAT_EYES 45

static u16 t;
static u8  spr_loaded;

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
static u8 shot_rect(s16 x, s16 y, u8 w, u8 h) {
    u8 k, n = 0;
    for (k = 0; k < nsh; k++) {
        Bullet *b;
        if ((s16)shx[k] < x || (s16)shx[k] >= x + w || (s16)shy[k] < y || (s16)shy[k] >= y + h) continue;
        b = &g_pshot[shi[k]];
        if (!b->on) continue;
        if (!b->pierce) b->on = 0;
        n++;
    }
    return n;
}
static void touch_rect(s16 x, s16 y, u8 w, u8 h) {
    s16 cx = PX(g_pl.x) + 8, cy = PX(g_pl.y) + 8;
    if (g_pl.alive && cx >= x && cx < x + w && cy >= y && cy < y + h) player_hit();
}

/* ======== ROTOR のコマ(面の入りで作る) ======== */
/* 0..7 歯車(32x32、元の半分の大きさ)、8..13 砂時計(64x64) */
static const u8 hg_ang[6] = { 0, 5, 11, 16, 21, 27 };
static u8 rj, rph, rrow;           /* 仕事の番号 / 0 = 元を読む、1 = 回す、2 = VRAM へ / 進んだ行 */
static u8 rot_n(void) { return rj < 8 ? 32 : 64; }
static void rot_update(void) {
    u8 n = rot_n();
    if (rj >= 14 || g_sec < SEC6_SINK) return;
    if (rph == 0) {
        if (rj == 0) rom_to_ram(ASSET_GEAR_BANK, ASSET_GEAR_OFF, ROTOR_SRC, 2048);
        if (rj == 8) rom_to_ram(ASSET_HOURGLASS_BANK, ASSET_HOURGLASS_OFF, ROTOR_SRC, 2048);
        rotor_begin(n, rj < 8 ? rj : hg_ang[rj - 8], rj < 8 ? 512 : 256, rj < 8 ? 2 : 1);
        rph = 1; rrow = 0;
    } else if (rph == 1) {                                    /* 1 フレーム 256 ドットほど(約 3ms) */
        u8 k = rj < 8 ? 8 : 4;
        rotor_rows(ROTOR_DST + (u16)rrow * (n >> 1), k);
        rrow += k;
        if (rrow >= n) { rph = 2; rrow = 0; }
    }
}
static void rot_vram(void) {       /* 出来たコマを 8 行ずつ VRAM へ(重いフレームは次へ) */
    extern u16 g_frame_t0;
    u8 k, n = rot_n();
    if (rj >= 14 || rph != 2 || (u16)(sys_timer() - g_frame_t0) >= 150u * 16u) return;
    for (k = 0; k < 8 && rrow < n; k++, rrow++) {
        u16 a;
        u8 hi = 1;
        if (rj < 8) a = (u16)(0x8000u + (u16)(68 + rrow) * 128u + rj * 16u);                       /* ページ 3 の 68 行 */
        else if (rj < 12) a = (u16)(0x8000u + (u16)(100 + rrow) * 128u + (rj - 8) * 32u);          /* ページ 3 の 100 行 */
        else a = (u16)((u16)(48 + rrow) * 128u + (rj - 12) * 32u);                                /* ページ 2 の 48 行 */
        vdp_write_addr17(hi, a);
        vdp_out_block(n >> 1, ROTOR_DST + (u16)rrow * (n >> 1));
    }
    if (rrow >= n) { rj++; rph = 0; }
}

/* ======== 歯車 ======== */
static u8 gf[S6_NGEAR], gshown[S6_NGEAR];
static void gear_vram(void) {
    u8 i;
    if (rj < 8) return;                                       /* 歯車のコマがまだ */
    for (i = 0; i < S6_NGEAR; i++) {
        s16 x = s6_sx(s6_gear[i]);
        u8 f;
        if (x < -32 || x > 256 || gq_free() < 6) continue;
        if (!stage_col_ready((u16)(g_w0 + s6_gear[i])) || !stage_col_ready((u16)(g_w0 + s6_gear[i] + 3))) continue;
        f = (u8)(((t >> 1) + i * 3) & 7);
        if (i & 1) f = (u8)(7 - f);                           /* 隣どうしは逆回り */
        if (gshown[i] == f) continue;
        ring_cmd((u16)(f * 32u), 768 + 68, x, s6_sy(34), 32, 32, 0, 0);
        gshown[i] = f;
    }
}

/* ======== 砂粒虫と影の目 ======== */
static u8 mt_hp[S6_NMITE];
static void mite_update(void) {
    u8 i;
    for (i = 0; i < S6_NMITE; i++) {
        s16 x = s6_sx(s6_mite[i]), y = s6_sy(34) - 16;
        if (!mt_hp[i] || x < -16 || x > 256) continue;
        if (shot_rect(x - 4, y, 16, 16) && --mt_hp[i] == 0) { fx_spawn(FX(x + 4), FX(y + 8)); score_add(50); continue; }
        if (((t + i * 21) & 63) == 0 && x > 8 && x < 240) enemy_bullet(FX(x + 4), FX(y + 12), -6, 22);   /* 砂粒 */
    }
}
static void mite_draw(void) {
    u8 i;
    for (i = 0; i < S6_NMITE; i++) {
        s16 x = s6_sx(s6_mite[i]);
        if (!mt_hp[i] || x < -16 || x > 256) continue;
        spr_put(x - 4, s6_sy(34) - 16, PAT_MITE, G_ENEMY, 0);
    }
}
static void eyes_draw(void) {      /* 影の帯の中の敵に目(色 8 は暗くしない) */
    u8 i;
    if (s6_shadow_b <= s6_shadow_a) return;
    for (i = 0; i < MAX_ENEMY; i++) {
        const Enemy *e = &g_enemy[i];
        s16 y;
        if (!e->type) continue;
        y = PX(e->y);
        if (y + 8 >= s6_shadow_a + 12 && y + 8 < s6_shadow_b) spr_put(PX(e->x), y, PAT_EYES, G_EBUL, 0);
    }
    for (i = 0; i < S6_NMITE; i++) {
        s16 x = s6_sx(s6_mite[i]), y = s6_sy(34) - 16;
        if (mt_hp[i] && x > -16 && x < 256 && y + 8 >= s6_shadow_a + 12 && y + 8 < s6_shadow_b) spr_put(x - 4, y, PAT_EYES, G_EBUL, 0);
    }
}

/* ======== 数字の砲台 ======== */
static const char *const num_str[12] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII" };
static const u8 num_len[12] = { 1, 2, 3, 2, 1, 2, 3, 4, 2, 1, 2, 3 };
static u8 nm_hp[S6_NNUM], nm_drawn[S6_NNUM];
#define NUM_S 34
static s16 num_x(u8 i) { return (s16)(s6_sx(s6_num[i]) + 12 - num_len[i] * 4); }
static void num_update(void) {
    u8 i;
    for (i = 0; i < S6_NNUM; i++) {
        s16 x = num_x(i), y = s6_sy(NUM_S);
        u8 w = (u8)(num_len[i] * 8);
        if (!nm_hp[i] || x < -32 || x > 256) continue;
        if (shot_rect(x, y - 2, w, 12) && --nm_hp[i] == 0) {
            fx_spawn(FX(x + (w >> 1)), FX(y + 4)); score_add(120);
            nm_drawn[i] = 2;                                  /* 空で塗る */
            continue;
        }
        if (((t + i * 17) & 127) == 0 && x > 16 && x < 232) enemy_shoot(FX(x + (w >> 1)), FX(y + 4));
    }
}
static void num_vram(void) {       /* 1 フレーム 1 個(CPU で VRAM を書く。ページの境をまたぐ位置では待つ) */
    u8 i;
    for (i = 0; i < S6_NNUM; i++) {
        s16 x = num_x(i);
        u16 rx;
        u8 w = (u8)(num_len[i] * 8), ry = (u8)(NUM_S + 188);
        if (x < -32 || x > 256 || nm_drawn[i] == 1 || nm_drawn[i] == 3) continue;
        if (nm_drawn[i] == 2) { if (gq_free() >= 4) { ring_cmd(0, 0, x, s6_sy(NUM_S), w, 8, 1, 0x11); nm_drawn[i] = 3; } return; }
        if (!stage_col_ready((u16)(g_w0 + s6_num[i])) || !stage_col_ready((u16)(g_w0 + s6_num[i] + 2))) continue;
        rx = (u16)(g_wx + (u16)x) & 511u;
        if ((u8)rx & 1 || (rx & 255u) + w > 256u) continue;
        vdp_cmd_wait();                                       /* ★列のコマンドの後に CPU で書く */
        font_text((u8)(rx >> 8), (u8)rx, ry, 15, 1, num_str[i]);
        nm_drawn[i] = 1;
        return;
    }
}

/* ======== HOURGLASS ======== */
#define HG_X  128                  /* 画面の左上(リングの見えない半分から寄せず、止めた画面に描く) */
#define HG_S  (-68)                /* 上端の行(空の行。ブロック 23 と YJK の行を避ける) */
static u8  hg_f, hg_shown, hg_chunk, hg_ph;
static u16 hg_t;
static u8  b_t;
void b6_mid_start(void) { hg_f = 0; hg_shown = 0xFF; hg_chunk = 0; hg_ph = 0; hg_t = 0; g_boss_state = BS_ENTER; g_boss_hp = 16; b_t = 0; }
static void hg_update(void) {
    s16 cy = s6_sy(HG_S + 32), cx = HG_X + 32;
    if (g_boss_state == BS_ENTER) { if (rj >= 14 && hg_shown == 0) g_boss_state = BS_FIGHT; return; }
    if (g_boss_state == BS_DYING) {
        if (!(b_t & 3)) fx_spawn(FX(HG_X + (rnd8() & 63)), FX(s6_sy(HG_S) + (rnd8() & 63)));
        if (++b_t == 90) hg_f = 0xFE;                         /* 空で塗る(b6_vram) */
        if (b_t > 100 && hg_shown == 0xFE) g_boss_state = BS_DONE;
        return;
    }
    hg_t++;
    if (hg_ph == 0) {                                         /* 止まっている: 括れから砂がこぼれる */
        if (hg_t < 100 && !(hg_t & 7)) enemy_bullet(FX(cx - 2 + (rnd8() & 3)), FX(cy + 4), (s16)((s16)(rnd8() & 15) - 8), 20 + (rnd8() & 7));
        if (hg_t >= 120) { hg_ph = 1; hg_t = 0; }
    } else {                                                  /* 180° 回る(コマ 1..5、10 フレームずつ)→ 0 */
        u8 f = 1;
        u16 tt = hg_t;
        while (tt >= 10) { tt -= 10; f++; }                   /* hg_t / 10(割り算はライブラリ呼び出し) */
        if (f > 5) { f = 0; hg_ph = 0; hg_t = 0; }
        hg_f = f;
        if (f >= 2 && f <= 4) {                               /* 括れ(弱点)が横を向いている */
            g_boss_aim = cy;
            if (shot_rect(cx - 8, cy - 8, 16, 16)) {
                if (g_boss_hp) g_boss_hp--;
                if (!g_boss_hp) { g_boss_state = BS_DYING; b_t = 0; score_add(3000); return; }
            }
        }
    }
    if (hg_ph == 0) shot_rect(HG_X, s6_sy(HG_S), 64, 64);     /* 止まっている間はガラスが弾を止める(回っている間は括れまで届く) */
    touch_rect(HG_X + 6, s6_sy(HG_S) + 4, 52, 56);
}
static void hg_vram(void) {        /* コマが替わったら 16 行ずつ 4 回で写す */
    u8 f = hg_f;
    u16 sx, sy;
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE || rj < 14 || gq_free() < 6) return;
    if (f == 0xFE) {
        if (hg_shown != 0xFE) { ring_cmd(0, 0, HG_X, s6_sy(HG_S), 64, 64, 1, 0x11); hg_shown = 0xFE; }
        return;
    }
    if (hg_shown == f && hg_chunk == 0) return;
    if (f < 4) { sx = (u16)(f * 64u); sy = 768 + 100; }
    else { sx = (u16)((f - 4) * 64u); sy = 512 + 48; }
    ring_cmd(sx, (u16)(sy + hg_chunk * 16u), HG_X, (s16)(s6_sy(HG_S) + hg_chunk * 16), 64, 16, 0, 0);
    if (++hg_chunk >= 4) { hg_chunk = 0; hg_shown = f; }
}

/* ======== GNOMON ======== */
#define GN_CX 150                  /* 軸の画面の位置 */
#define GN_CY 108
#define GN_R  120
#define GN_Y0 (SPLIT_H + 2)        /* 針を描く上下の限り(SCREEN12 の帯と床を避ける) */
#define GN_Y1 175
static u16 gn_a;                   /* 角度(8.8、上位 = 64 で一周) */
static s16 gn_spd;
static u16 gn_turn;                /* 今の周で回った量 */
static u8  gn_lap;
static s8  gn_cs, gn_sn;           /* 今の向き(×127) */
static s16 old_x[3], old_y[3], old_dx[3], old_dy[3];
static u8  old_n;
void b6_boss_start(void) { gn_a = 0x3000; gn_spd = 48; gn_turn = 0; gn_lap = 0; old_n = 0; g_boss_state = BS_ENTER; g_boss_hp = 40; b_t = 0; }
static s8 sin_i(u16 a) {           /* 64 刻みの表を線形補間 */
    u8 i = (u8)(a >> 8) & 63, f = (u8)a;
    s16 s0 = g_sin64[i], s1 = g_sin64[(i + 1) & 63];
    return (s8)(s0 + (((s1 - s0) * f) >> 8));
}
static void gn_update(void) {
    s16 px, py, along, cross;
    if (g_boss_state == BS_ENTER) { g_boss_state = BS_FIGHT; return; }
    if (g_boss_state == BS_DYING) {
        if (!(b_t & 3)) fx_spawn(FX(GN_CX - 32 + (rnd8() & 63)), FX(GN_CY - 32 + (rnd8() & 63)));
        if (++b_t > 120 && !old_n) g_boss_state = BS_DONE;
        return;
    }
    gn_a += (u16)gn_spd;
    gn_turn += (u16)(gn_spd < 0 ? -gn_spd : gn_spd);
    if (gn_turn >= 0x4000u) {                                 /* 一周: 速く。3 周目から逆回り */
        gn_turn -= 0x4000u; gn_lap++;
        gn_spd = (s16)(gn_spd + (gn_spd >> 1));
        if (gn_lap == 2) gn_spd = (s16)-gn_spd;
        if (gn_spd > 160) gn_spd = 160; else if (gn_spd < -160) gn_spd = -160;
    }
    gn_cs = sin_i((u16)(gn_a + 0x1000u)); gn_sn = sin_i(gn_a);
    /* 針に触れたら被弾(軸からの向きの成分と横の距離。桁あふれしないよう座標を半分に) */
    px = (s16)((PX(g_pl.x) + 8 - GN_CX) >> 1); py = (s16)((PX(g_pl.y) + 8 - GN_CY) >> 1);
    along = (s16)(px * gn_cs + py * gn_sn); cross = (s16)(px * gn_sn - py * gn_cs);
    if (g_pl.alive && along > 0 && along < (GN_R / 2) * 127 && cross > -3 * 127 && cross < 3 * 127) player_hit();
    /* 根元: 弱点。針の向きへ撃つ */
    g_boss_aim = GN_CY;
    if (shot_rect(GN_CX - 9, GN_CY - 9, 18, 18)) {
        if (g_boss_hp) g_boss_hp--;
        if (!g_boss_hp) { g_boss_state = BS_DYING; b_t = 0; score_add(6000); return; }
    }
    if (!(t & 15)) enemy_bullet(FX(GN_CX + (gn_cs >> 3)), FX(GN_CY + (gn_sn >> 3)), (s16)(gn_cs >> 2), (s16)(gn_sn >> 2));
}
static u8 gn_inside(s16 r) {
    s16 x = (s16)(GN_CX + ((r * gn_cs) >> 7)), y = (s16)(GN_CY + ((r * gn_sn) >> 7));
    return (u8)(x >= 0 && x <= 255 && y >= GN_Y0 && y <= GN_Y1);
}
static void gn_vram(void) {        /* 2 フレームに 1 回: 前の針を空の色で消し、今の針を描き、根元を描き直す */
    u8 k;
    s16 lo = 0, hi = GN_R, ex, ey;
    if ((t & 1) || gq_free() < 10) return;
    for (k = 0; k < old_n; k++) gq_line((u16)old_x[k], (u16)(old_y[k] + 68), old_dx[k], old_dy[k], 1);
    old_n = 0;
    if (g_boss_state == BS_DYING || g_boss_state == BS_DONE || g_boss_state == BS_NONE) {
        if (g_boss_state == BS_DYING && b_t > 100) ring_cmd(0, 0, GN_CX - 10, GN_CY - 10, 20, 20, 1, 0x11);
        return;
    }
    while (hi - lo > 1) { s16 m = (s16)((lo + hi) >> 1); if (gn_inside(m)) lo = m; else hi = m; }   /* 画面の中で切る */
    ex = (s16)((lo * gn_cs) >> 7); ey = (s16)((lo * gn_sn) >> 7);
    for (k = 0; k < 3; k++) {
        s8 o = (s8)(k - 1);
        s16 x0 = GN_CX, y0 = GN_CY;
        if ((gn_cs < 0 ? -gn_cs : gn_cs) > (gn_sn < 0 ? -gn_sn : gn_sn)) y0 += o; else x0 += o;
        if (y0 + ey < GN_Y0 || y0 + ey > GN_Y1 || x0 + ex < 0 || x0 + ex > 255) continue;
        gq_line((u16)x0, (u16)(y0 + 68), ex, ey, 2);          /* リングの行 = 画面の行 + 68(ページ 0) */
        old_x[old_n] = x0; old_y[old_n] = y0; old_dx[old_n] = ex; old_dy[old_n] = ey; old_n++;
    }
    ring_cmd(0, 0, GN_CX - 8, GN_CY - 8, 16, 16, 1, 0x55);   /* 根元 */
    ring_cmd(0, 0, GN_CX - 4, GN_CY - 4, 8, 8, 1, 0xDD);
}

/* ======== 入口 ======== */
void b6_init(void) {
    u8 i;
    t = 0; spr_loaded = 0; rj = 0; rph = 0; rrow = 0;
    for (i = 0; i < S6_NGEAR; i++) { gf[i] = 0; gshown[i] = 0xFF; }
    for (i = 0; i < S6_NMITE; i++) mt_hp[i] = 2;
    for (i = 0; i < S6_NNUM; i++) { nm_hp[i] = 3; nm_drawn[i] = 0; }
}

void b6_update(void) {
    if (g_boss_dmg) {
        u8 d = g_boss_dmg; g_boss_dmg = 0;
        if (g_boss_state == BS_FIGHT) g_boss_hp = g_boss_hp > d ? (u8)(g_boss_hp - d) : 1;
    }
    t++;
    g_boss_aim = -1;
    shots_prepare();
    rot_update();
    if (g_sec == SEC6_WALL || g_sec == SEC6_SHADOW) mite_update();
    if (g_sec == SEC6_DIAL || g_sec == SEC6_BOSS) num_update();
    if (g_sec == SEC6_MID && g_boss_state != BS_NONE && g_boss_state != BS_DONE) hg_update();
    if (g_sec == SEC6_BOSS && g_boss_state != BS_NONE && g_boss_state != BS_DONE) gn_update();
}

void b6_draw(void) {
    if (g_sec == SEC6_WALL || g_sec == SEC6_SHADOW) { mite_draw(); eyes_draw(); }
}

void b6_vram(void) {
    if (!spr_loaded) { rom_to_vram(ASSET_S6_SPR_BANK, ASSET_S6_SPR_OFF, 1, (u16)(0xF800u + PAT_MITE * 32u), 64); spr_loaded = 1; }
    rot_vram();
    if (g_sec == SEC6_WALL || g_sec == SEC6_SHADOW) gear_vram();
    if (g_sec == SEC6_DIAL || g_sec == SEC6_BOSS) num_vram();
    if (g_sec == SEC6_MID) hg_vram();
    if (g_sec == SEC6_BOSS) gn_vram();
    gq_pump(0);
}

void b6_bands(void) { }
