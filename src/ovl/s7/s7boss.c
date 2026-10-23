/* s7boss.c — 7 面の最終試験 LIBRA(天秤、指示書 5.7)。
   ・測定空間(白い方眼)に浮かぶ天秤。竿は VDP の LINE(3 本の平行線、傾きが変わったときだけ前の線を空の色で
     消して描き直す)。支柱は BG の塗り。皿・分銅・測定値のアイコンはスプライト。
   ・左の皿に Infinitas の測定値(強化の段階の合計 = アイコンの数)。段階が高いほど竿が自機の側へ傾き、皿からの
     弾が増える。
   ・LIMIT BREAK(強化を崩す。本体の boss_damage が届く)で竿が跳ね上がり、分銅が落ちて支点の核(弱点)が
     8 秒だけ出る。その間だけ撃てる。
   ・画面がリングのページ 0 にそろう位置で世界を止める(LINE の座標 = 画面の座標)。 */
#include "s7.h"
#include "spr.h"
#include "vdp.h"
#include "ghost.h"
#include "sprite_data.h"

#define PAT_PAN    44
#define PAT_WEIGHT 45
#define PAT_ICON   46
#define CX   128
#define CY   80
#define ARM  80

static u16 t;
static u8  spr_loaded;
static s8  tilt, tilt_to;          /* 竿の右端の上下(ドット。正 = 右が下 = 分銅の側が重い) */
static s8  tilt_drawn;
static u8  drawn;                  /* 竿を描いてある */
static u8  expose;                 /* 弱点が出ている残り(フレーム / 4) */
static s16 wy;                     /* 分銅の落下(ドット、0 = 皿の上) */
static u8  b_t, post, fire_t, hit_t, defl_t;
static s16 defl_x, defl_y;          /* 効かない所に当たった弾の火花 */

static const u8 pat_pan[32] = {    /* 皿(16x16 の下半分に) */
    0,0,0,0,0,0,0,0,0x80,0xC0,0x7F,0x3F,0x1F,0,0,0, 0,0,0,0,0,0,0,0,0x01,0x03,0xFE,0xFC,0xF8,0,0,0 };
static const u8 pat_weight[32] = { /* 分銅(取っ手つきの台形) */
    0,0x03,0x04,0x04,0x03,0x0F,0x1F,0x1F,0x3F,0x3F,0x7F,0x7F,0xFF,0xFF,0,0,
    0,0xC0,0x20,0x20,0xC0,0xF0,0xF8,0xF8,0xFC,0xFC,0xFE,0xFE,0xFF,0xFF,0,0 };
static const u8 pat_icon[32] = {   /* 測定値のアイコン(小さな ∞) */
    0,0,0,0,0,0x38,0x44,0x82,0x82,0x44,0x38,0,0,0,0,0, 0,0,0,0,0,0x1C,0x22,0x41,0x41,0x22,0x1C,0,0,0,0,0 };

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

static u8 level(void) {            /* 強化の段階の合計(測定値) */
    const Gauge *g = &g_gauge;
    u8 n = (u8)(g->thrust + g->echo + g->aegis + g->drop + g->split + g->ray);
    return n > 8 ? 8 : n;
}

void b7_init(void) { t = 0; spr_loaded = 0; drawn = 0; post = 0; }
void b7_start(void) {
    g_boss_state = BS_ENTER; g_boss_hp = 30; tilt = tilt_to = 0; tilt_drawn = 0x7F; expose = 0; wy = 0; b_t = 0; fire_t = 0; hit_t = 0; defl_t = 0;
}

void b7_update(void) {
    u8 lv;
    t++;
    g_boss_aim = -1;
    if (g_sec != SEC7_LIBRA || g_boss_state == BS_NONE || g_boss_state == BS_DONE) return;
    if (g_boss_state == BS_ENTER) {                           /* 画面がページ 0 にそろうまで流してから止める */
        if ((g_wx & 511u) < 4) {
            g_scroll_speed = 0;
            if ((g_wx & 511u) == 0 && post) { g_boss_state = BS_FIGHT; stage_banner_c("HINT: USE LIMIT BREAK", 13); }   /* ★倒し方の合図 */
        }
        g_boss_dmg = 0;
        return;
    }
    if (g_boss_state == BS_DYING) {
        if (!(b_t & 3)) fx_spawn(FX(CX - ARM + (rnd8() & 127) + (rnd8() & 31)), FX(CY - 8 + (rnd8() & 31)));
        if (++b_t > 120) g_boss_state = BS_DONE;
        return;
    }
    shots_prepare();
    if (hit_t) hit_t--;
    if (defl_t) defl_t--;
    lv = level();
    if (g_boss_dmg) { g_boss_dmg = 0; tilt_to = 24; expose = 120; wy = 1; }   /* LIMIT BREAK: 竿が跳ね上がり分銅が落ちる */
    if (expose) {
        if (!(t & 3)) expose--;
        if (wy && wy < 200) wy = (s16)(wy + (wy >> 3) + 1);
        g_boss_aim = CY;
        if (shot_rect(CX - 10, CY - 10, 20, 20)) {
            hit_t = 8;                                        /* ★当たった印(当てても光らず、どこで傷が入るか分からなかった。遊んでの指摘) */
            if (g_boss_hp) g_boss_hp--;
            if (!g_boss_hp) { g_boss_state = BS_DYING; b_t = 0; score_add(8000); return; }
        }
        if (!expose) wy = 0;                                  /* 分銅が戻る */
    } else {
        tilt_to = (s8)(-(s8)lv * 3);                          /* 段階が高いほど自機の側(左)が下 */
        if (!(t & 511)) item_spawn(FX(CX + ARM), FX(CY + tilt), 1);   /* ★分銅の皿から Infinity Shard(∞が無いと核が出せず詰んだ) */
    }
    if (tilt < tilt_to) tilt++; else if (tilt > tilt_to) tilt--;
    /* 皿と分銅は弾を止める(皿の厚みだけ。支点の核へ撃てるように)。★止めた所に白い火花(効かない所に撃っても何も起きず、
       どこで傷が入るか分からなかった。遊んでの指摘「ダメージで点滅しない」) */
    if (shot_rect(CX - ARM - 10, CY - tilt + 14, 20, 8)) { defl_x = CX - ARM - 10; defl_y = CY - tilt + 18; defl_t = 4; }
    if (shot_rect(CX + ARM - 10, CY + tilt + 14, 20, 8)) { defl_x = CX + ARM - 10; defl_y = CY + tilt + 18; defl_t = 4; }
    if (wy < 190 && shot_rect(CX + ARM - 8, CY + tilt - 2 + wy, 16, 14)) { defl_x = CX + ARM - 8; defl_y = CY + tilt + 4 + wy; defl_t = 4; }
    {   /* 皿から撃つ: 測定値が高いほど速く */
        if (++fire_t >= (u8)(64 - lv * 6)) {
            fire_t = 0; enemy_shoot(FX(CX - ARM), FX(CY - tilt + 8)); enemy_shoot(FX(CX + ARM), FX(CY + tilt + 8)); }
        if (lv >= 4 && !((t + 16) & 31)) enemy_bullet(FX(CX), FX(CY), (s16)((s16)(rnd8() & 63) - 32), 24);
    }
}

void b7_draw(void) {
    u8 i, lv;
    if (g_sec != SEC7_LIBRA || g_boss_state == BS_NONE || g_boss_state == BS_DONE || g_boss_state == BS_ENTER) return;
    lv = level();
    spr_put(CX - ARM - 8, CY - tilt + 8, PAT_PAN, G_ENEMY, 0);    /* 左の皿(竿の左端 = CY - tilt) */
    spr_put(CX + ARM - 8, CY + tilt + 8, PAT_PAN, G_ENEMY, 0);
    for (i = 0; i < lv && i < 4; i++) spr_put(CX - ARM - 14 + i * 7, CY - tilt + 2 - (i & 1) * 6, PAT_ICON, G_MISC, CS_CURSOR);
    if (wy < 190) spr_put(CX + ARM - 8, CY + tilt - 2 + wy, PAT_WEIGHT, G_ENEMY, 0);
    /* 核: 当たったら火花と点滅(2 フレームごとに消す)。効かない所に当たった弾は白い火花 */
    if (hit_t) { if (hit_t & 2) spr_put(CX - 8, CY - 8, PAT_EXPL1, G_MISC, CS_EXPL); }
    else if (expose && (t & 4)) spr_put(CX - 8, CY - 8, PAT_ICON, G_MISC, CS_EXPL);   /* 支点の核 */
    if (defl_t) spr_put(defl_x - 4, defl_y - 8, PAT_EXPL1, G_MISC, CS_CURSOR);
}

/* 竿: 傾きが変わったら、前の 3 本を空の色(1)で消して描き直す。リングの行 = 画面の行 + g_vscroll */
static void beam(s8 tl, u8 col) {
    u8 k;
    u8 y0 = (u8)(CY - tl + g_vscroll);
    for (k = 0; k < 3; k++) gq_line(CX - ARM, (u16)(u8)(y0 + k - 1), ARM * 2, (s16)(tl * 2), col);
}
void b7_vram(void) {
    if (!spr_loaded) {
        vdp_write_addr17(1, (u16)(0xF800u + PAT_PAN * 32u)); vdp_out_block(32, pat_pan);
        vdp_write_addr17(1, (u16)(0xF800u + PAT_WEIGHT * 32u)); vdp_out_block(32, pat_weight);
        vdp_write_addr17(1, (u16)(0xF800u + PAT_ICON * 32u)); vdp_out_block(32, pat_icon);
        spr_loaded = 1;
    }
    if (g_sec != SEC7_LIBRA || gq_free() < 10) { gq_pump(0); return; }
    /* ★支柱はカメラが測定空間の高さに着き、空の描き替えが終わってから(試験 06 から来ると、カメラが上がる途中で描いた支柱の
       上が、空の描き直しで消えた。遊んでの指摘「どこが弱点かわからない」で見つけた) */
    if (g_boss_state == BS_ENTER && !post && !g_scroll_speed && (g_wx & 511u) == 0 && g_scam_q <= -192 * 16 && !stage_busy()) {
        ring_cmd(0, 0, CX - 4, CY, 8, 80, 1, 0x99);           /* 支柱 */
        ring_cmd(0, 0, CX - 24, CY + 80, 48, 6, 1, 0x55);
        post = 1;
    }
    if (g_boss_state == BS_FIGHT || g_boss_state == BS_DYING) {
        /* ★64 フレームごとに支柱と竿を描き直す(7 面に戻ると空の方眼を何秒もかけて描き直し、その途中で描いた支柱の上と竿が
           上書きされて見えなかった。stage_busy はブロックの合間に 0 を返すので待てない。遊んでの指摘で見つけた) */
        if (g_boss_state == BS_FIGHT && !(t & 63) && drawn) {
            ring_cmd(0, 0, CX - 4, CY, 8, 80, 1, 0x99);
            ring_cmd(0, 0, CX - 24, CY + 80, 48, 6, 1, 0x55);
            beam(tilt_drawn, 9);
        } else if (tilt != tilt_drawn && !(t & 1)) {
            if (drawn) beam(tilt_drawn, 1);
            beam(tilt, 9);                                    /* ★支柱と同じ青灰(色 2 は測定空間の白と同じで見えなかった) */
            ring_cmd(0, 0, CX - 6, CY - 6, 12, 12, 1, 0xDD);  /* 支点 */
            tilt_drawn = tilt; drawn = 1;
        }
    }
    if (g_boss_state == BS_DYING && b_t > 100 && drawn && gq_free() >= 10) {   /* 壊れた: 竿と支柱を消す */
        beam(tilt_drawn, 1);
        ring_cmd(0, 0, CX - 24, CY - 6, 48, 92, 1, 0x11);
        drawn = 0;
    }
    gq_pump(0);
}

void b7_bands(void) { }

void b7_invert(u16 tt) { if (tt == 60) { g_hall = 0; g_sky_t = 16; } }   /* 白 → 黒(方眼は残る) */
