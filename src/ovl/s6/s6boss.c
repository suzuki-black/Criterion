/* s6boss.c — 6 面の歯車・砂粒虫・数字の砲台、中ボス HOURGLASS、ボス GNOMON(指示書 5.6)。
   ・歯車(ROTOR で回す)と砂時計(ROM の絵)のコマを面の入りで作ってページ 3(68 行から下: 6 面の短冊は 0..67 行しか
     使わない)とページ 2 に置き、HMMM で表示リングへ写す(歯車 8 コマ = 45°(歯 8 枚)で一周、30fps。砂時計 5 コマ = 0..146°)。
   ・砂粒虫: 外壁の棚のスプライト。砂粒(落ちる弾)をこぼす。影の中では敵の体の色(12)が暗くなり、目(弾の色 8 の
     スプライト)だけが光る。
   ・HOURGLASS: 96x96(C3。VRAM に置けるのは 5 コマ)。3 秒ごとに 180° 回り、回っている間だけ括れ
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
/* 0..7 歯車(32x32、元の半分の大きさ。ROTOR)、8..12 砂時計(96x96。★C3: 64x64 から大きくした。ROTOR で 1.5 倍にすると
   縁が角ばるので、gen_stage6.py で 5 つの角度を描き直して ROM に置き、写すだけ。角度は HG96_ANG = 0..146°) */
#define HG_N    96
#define HG_NF   5                  /* 砂時計のコマ(★96x96 は VRAM に 5 コマまで) */
#define RJ_END  (8 + HG_NF)
/* 砂時計のコマは 16 行ずつの片に分けて置く(片 g = コマ × 6 + 段)。96 幅の列 2 本 × (ページ 2 の 48..207 行 =
   片 10 個、ページ 3 の 100..179 行 = 片 5 個) = 30 片。★ページ 2 の 160..183 行は面の開始表示の帯でも使うが、
   砂時計を作るのは 6 面の帯が終わってからで、次の面の帯は砂時計の後。ページ 3 の 180 行より先と 192 行〜は使わない */
static u16 hg_sy;
static u8  hg_sx;
static void hg_slot(u8 g) {
    if (g < 20) { hg_sx = g >= 10 ? 96 : 0; if (g >= 10) g -= 10; hg_sy = (u16)(512 + 48 + g * 16u); }
    else { g -= 20; hg_sx = g >= 5 ? 96 : 0; if (g >= 5) g -= 5; hg_sy = (u16)(768 + 100 + g * 16u); }
}
extern u16 g_frame_t0;             /* main.c: このフレームの始まり */
static u8 rj, rph, rrow;           /* 仕事の番号 / 0 = 元を読む、1 = 回す、2 = VRAM へ / 進んだ行 */
static void rot_update(void) {
    if (rj >= RJ_END || g_sec < SEC6_SINK) return;
    if (rph == 0) {
        if (rj >= 8) { rrow = 0; rph = 2; return; }           /* 砂時計: 描いた絵(ROM)を写すだけ */
        if (rj == 0 && rrow < 4) {                            /* 元の絵(2KB)は 4 回に分けて(★1 回で読むと約 80 ライン、試験 6 で遅れた) */
            rom_to_ram(ASSET_GEAR_BANK, (u16)(ASSET_GEAR_OFF + rrow * 512u), ROTOR_SRC + rrow * 512u, 512);
            rrow++;
            return;
        }
        rrow = 0;
        rotor_begin(32, rj, 512, 2);
        rph = 1;
    } else if (rph == 1) {                                    /* 1 フレーム 256 ドットほど(約 3ms) */
        rotor_rows(ROTOR_DST + (u16)rrow * 16u, 8);
        rrow += 8;
        if (rrow >= 32) { rph = 2; rrow = 0; }
    }
}
static void rot_vram(void) {       /* 出来たコマを 8 行(砂時計は 4 行)ずつ VRAM へ(重いフレームは次へ) */
    u8 k, n = rj < 8 ? 32 : HG_N;
    if (rj >= RJ_END || rph != 2 || (u16)(sys_timer() - g_frame_t0) >= 150u * 16u) return;
    for (k = rj < 8 ? 8 : 4; k && rrow < n; k--, rrow++) {    /* 砂時計(ROM から)は 4 行ずつ(8 行で約 50 ライン) */
        if (rj < 8) {
            vdp_write_addr17(1, (u16)(0x8000u + (u16)(68 + rrow) * 128u + rj * 16u));            /* ページ 3 の 68 行 */
            vdp_out_block(16, ROTOR_DST + (u16)rrow * 16u);
        } else {
            hg_slot((u8)((rj - 8) * 6 + (rrow >> 4)));
            rom_to_vram(ASSET_HG96_BANK, (u16)(ASSET_HG96_OFF + ((u16)(rj - 8) * HG_N + rrow) * (HG_N / 2)), 1,
                        (u16)((u16)(hg_sy - 512 + (rrow & 15)) * 128u + (hg_sx >> 1)), HG_N / 2);   /* ページ 2|3 */
        }
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
        if ((LB_NOW ? (mt_hp[i] = 1) : shot_rect(x - 4, y, 16, 16)) && --mt_hp[i] == 0) { fx_spawn(FX(x + 4), FX(y + 8)); score_add(50); continue; }
        if (((t + i * 21) & 63) == 0 && x > 8 && x < 240) enemy_shoot(FX(x + 4), FX(y + 4));   /* 砂粒。★自機を狙う(真下の壁へ落とすだけで意味が分からなかった。遊んでの指摘) */
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
/* ======== 数字の砲台 ======== */
static const char *const num_str[12] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII" };
static const u8 num_len[12] = { 1, 2, 3, 2, 1, 2, 3, 4, 2, 1, 2, 3 };
static u8 nm_hp[S6_NNUM], nm_drawn[S6_NNUM];
#define NUM_S 34
static s16 num_x(u8 i) { return (s16)(s6_sx(s6_num[i]) + 12 - num_len[i] * 4); }
u8 b6_num_alive(u16 col) {
    u8 i;
    if (g_sec != SEC6_DIAL) return 0;
    for (i = 0; i < S6_NNUM; i++) if ((u16)(col - s6_num[i]) < 3) return nm_hp[i];
    return 0;
}
static void num_update(void) {
    u8 i;
    for (i = 0; i < S6_NNUM; i++) {
        s16 x = num_x(i), y = s6_sy(NUM_S);
        u8 w = (u8)(num_len[i] * 8);
        if (!nm_hp[i] || x < -32 || x > 256) continue;
        if ((LB_NOW ? (nm_hp[i] = 1) : shot_rect(x, y - 2, w, 12)) && --nm_hp[i] == 0) {
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
        if (vdp_cmd_busy() && (u16)(sys_timer() - g_frame_t0) > 100u * 16u) return;   /* ★残りが少なければ次へ(列のコマンドを 100 ライン待って試験 6 で遅れた) */
        vdp_cmd_wait();                                       /* ★列のコマンドの後に CPU で書く */
        font_text((u8)(rx >> 8), (u8)rx, ry, 15, 1, num_str[i]);
        nm_drawn[i] = 1;
        return;
    }
}

/* ======== HOURGLASS ======== */
#define HG_X  112                  /* 画面の左上(リングの見えない半分から寄せず、止めた画面に描く。中心は 160) */
#define HG_S  (-68)                /* 上端の行(空の行。ブロック 23 と YJK の行を避ける) */
static u8  hg_f, hg_shown, hg_chunk, hg_ph, hg_drawing, hg_hit;
static u16 hg_t;
static u8  b_t;
static const s8 hg_ring[8][2] = { { 24, 0 }, { 17, 17 }, { 0, 24 }, { -17, 17 }, { -24, 0 }, { -17, -17 }, { 0, -24 }, { 17, -17 } };
/* ======== 砂(指示書 5.6「上部に溜まった砂(BG のドット粒子)が下部へ落ちる」。2026-10-29) ========
   砂を、汎用の敵弾ではなく自分で動かす粒にする(最大 NSAND。落ちる間はスプライト、当たると被弾)。
   ・括れからこぼれる砂: ガラスの中を落ち、下に溜まった砂の上(lim)で消える(★床まで落とすとガラスの底を突き抜けて見えた)。
   ・回り終わりに 8 方向へ散る砂: 外へ飛んで重力で落ち、床に着いたら消えて、その 4 ドットの列の床の上に BG の 4x2 を
     描き足す(列ごとの高さ pile。積もって山になる)。着地 1 回に VDP のコマンド 1 つ。山は止めた画面に描くので、戦いが終わって
     流れれば列ごと消える */
#define NSAND 12
static s16 sd_x[NSAND], sd_y[NSAND], sd_vy[NSAND];   /* 固定小数点 */
static s8  sd_vx[NSAND];
static u8  sd_on[NSAND], sd_lim[NSAND], pile[64];  /* sd_lim: 0 = 床に積もる、それ以外 = この画面の y で消える */
static void sand_spawn(s16 x, s16 y, s8 vx, s16 vy, u8 lim) {
    u8 i;
    for (i = 0; i < NSAND; i++) if (!sd_on[i]) {
        sd_on[i] = 1; sd_x[i] = FX(x); sd_y[i] = FX(y); sd_vx[i] = vx; sd_vy[i] = vy; sd_lim[i] = lim;
        return;
    }
}
static void sand_update(void) {
    u8 i;
    for (i = 0; i < NSAND; i++) {
        s16 x, y, fy;
        u8 c;
        if (!sd_on[i]) continue;
        if (sd_vy[i] < FX(4)) sd_vy[i] += 3;                  /* 重力 */
        sd_x[i] += sd_vx[i]; sd_y[i] += sd_vy[i];
        x = PX(sd_x[i]); y = PX(sd_y[i]);
        if (x < 0 || x > 252) { sd_on[i] = 0; continue; }
        if (g_pl.alive && (u16)(PX(g_pl.x) + 12 - x) < 9 && (u16)(PX(g_pl.y) + 12 - y) < 9) { player_hit(); sd_on[i] = 0; continue; }
        if (sd_lim[i]) { if (y >= sd_lim[i]) sd_on[i] = 0; continue; }
        c = (u8)(x >> 2);
        fy = (s16)(s6_sy(s6_top[g_map[s6_col_at(x)]]) - pile[c]);
        if (y < fy) continue;
        sd_on[i] = 0;                                         /* 着いた: 積もる */
        if (pile[c] < 24 && gq_free() >= 4) { ring_cmd(0, 0, (s16)(x & ~3), (s16)(fy - 2), 4, 2, 1, 0x77); pile[c] += 2; }
    }
}
static void sand_draw(void) {
    u8 i;
    for (i = 0; i < NSAND; i++) if (sd_on[i]) spr_put(PX(sd_x[i]) - 8, PX(sd_y[i]) - 8, PAT_EBUL, G_EBUL, 0);
}

void b6_mid_start(void) { { u8 i; for (i = 0; i < NSAND; i++) sd_on[i] = 0; for (i = 0; i < 64; i++) pile[i] = 0; } hg_f = 0; hg_shown = 0xFF; hg_chunk = 0; hg_drawing = 0; hg_hit = 0; hg_ph = 0; hg_t = 0; g_boss_state = BS_ENTER; g_boss_hp = 16; b_t = 0; }
static void hg_update(void) {
    s16 cy = s6_sy(HG_S + HG_N / 2), cx = HG_X + HG_N / 2;
    if (g_boss_state == BS_ENTER) { if (rj >= RJ_END && hg_shown == 0) g_boss_state = BS_FIGHT; return; }
    if (g_boss_state == BS_DYING) {
        if (!(b_t & 3)) fx_spawn(FX(HG_X + (rnd8() & 63) + (rnd8() & 31)), FX(s6_sy(HG_S) + (rnd8() & 63) + (rnd8() & 31)));
        if (++b_t == 90) hg_f = 0xFE;                         /* 空で塗る(b6_vram) */
        if (b_t > 100 && hg_shown == 0xFE) g_boss_state = BS_DONE;
        return;
    }
    hg_t++;
    if (hg_hit) hg_hit--;
    if (hg_ph == 0) {                                         /* 止まっている: 括れから砂がこぼれ、上下の膨らみから自機を狙う */
        /* ★真下に砂を落とすだけだった(遊んでの指摘「ずっと真下に球を吐いている」)。砂は前半だけ、狙い撃ちと回り終わりの 8 方向を足す */
        if (hg_t < 64 && !(hg_t & 3)) sand_spawn((s16)(cx - 2 + (rnd8() & 3)), (s16)(cy + 4), (s8)((rnd8() & 7) - 4), FX(1), (u8)(cy + 26));   /* 括れから下の砂へこぼれる */
        if (hg_t == 30 || hg_t == 80) { enemy_shoot(FX(cx), FX(cy - 28)); enemy_shoot(FX(cx), FX(cy + 28)); }
        if (hg_t >= 120) { hg_ph = 1; hg_t = 0; }
    } else {                                                  /* 180° 回る(コマ 1..4、13 フレームずつ)→ 0 */
        u8 f = 1;
        u16 tt = hg_t;
        while (tt >= 13) { tt -= 13; f++; }                   /* hg_t / 13(割り算はライブラリ呼び出し) */
        if (f >= HG_NF) {                                     /* 回り終わり: 括れから 8 方向へ砂を散らす */
            u8 k;
            f = 0; hg_ph = 0; hg_t = 0;
            for (k = 0; k < 8; k++) sand_spawn(cx, cy, (s8)(hg_ring[k][0] + (rnd8() & 15) - 8), hg_ring[k][1], 0);   /* 外へ飛んで床に積もる(向きを揺らして山をならす) */
        }
        hg_f = f;
        if (f >= 2 && f <= 3) {                               /* 括れ(弱点)が横を向いている(73°・107°。26 フレーム) */
            g_boss_aim = cy;
            if (shot_rect(cx - 12, cy - 12, 24, 24)) {
                hg_hit = 4;                                   /* ★当たった印(当てても光らなかった。遊んでの指摘) */
                if (g_boss_hp) g_boss_hp--;
                if (!g_boss_hp) { g_boss_state = BS_DYING; b_t = 0; score_add(3000); return; }
            }
        }
    }
    if (hg_ph == 0) shot_rect(HG_X, s6_sy(HG_S), HG_N, HG_N); /* 止まっている間はガラスが弾を止める(回っている間は括れまで届く) */
    touch_rect(HG_X + 9, s6_sy(HG_S) + 6, 78, 84);
}
static void hg_vram(void) {        /* コマが替わったら 16 行ずつ 6 回で写す */
    u8 f = hg_f;
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE || rj < RJ_END || gq_free() < 6) return;
    if (f == 0xFE) {
        if (hg_shown != 0xFE) { ring_cmd(0, 0, HG_X, s6_sy(HG_S), HG_N, HG_N, 1, 0x11); hg_shown = 0xFE; }
        return;
    }
    if (hg_shown == f && hg_chunk == 0) return;
    /* ★写している途中でコマが替わったら頭から写し直す(続きの行だけ次のコマで写し、上の行が前のコマのまま残って、
       立っている砂時計と回った砂時計が重なって見えた。遊んでの指摘) */
    if (hg_chunk && f != hg_drawing) hg_chunk = 0;
    hg_drawing = f;
    hg_slot((u8)(f * 6 + hg_chunk));
    ring_cmd(hg_sx, hg_sy, HG_X, (s16)(s6_sy(HG_S) + hg_chunk * 16), HG_N, 16, 0, 0);
    if (++hg_chunk >= HG_N / 16) { hg_chunk = 0; hg_shown = f; }
}

/* ======== GNOMON ======== */
#define GN_CX 128                  /* 軸の画面の位置(プレイエリアの中央。指示書 5.6「中心を軸に」) */
#define GN_CY 112
#define GN_R  220                  /* 長針(200 ドット超。画面の端で切って描く。当たりの計算は 16 ビットに収まる) */
#define GN_Y0 (SPLIT_H + 2)        /* 針を描く上下の限り(SCREEN12 の帯と床を避ける) */
#define GN_Y1 175
static u16 gn_a[2];                /* 角度(8.8、上位 = 64 で一周)。[1] は再校正版の短針 */
static s16 gn_spd;
static u16 gn_turn;                /* 今の周で回った量 */
static u8  gn_lap, gn_nh;
static s8  gn_cs[2], gn_sn[2];     /* 今の向き(×127) */
static const u8 gn_len[2] = { GN_R, 64 };
static s16 old_x[6], old_y[6], old_dx[6], old_dy[6];
static u8  old_n;
void b6_boss_start(void) {
    gn_a[0] = 0x3000; gn_a[1] = 0x1000; gn_spd = 48; gn_turn = 0; gn_lap = 0; old_n = 0; hg_hit = 0;
    gn_nh = RECAL ? 2 : 1;                                  /* 再校正版: 長針と短針 */
    g_boss_state = BS_ENTER; g_boss_hp = 40; b_t = 0;
}
static s8 sin_i(u16 a) {           /* 64 刻みの表を線形補間 */
    u8 i = (u8)(a >> 8) & 63, f = (u8)a;
    s16 s0 = g_sin64[i], s1 = g_sin64[(i + 1) & 63];
    return (s8)(s0 + (((s1 - s0) * f) >> 8));
}
static void gn_update(void) {
    s16 px, py;
    u8 h;
    if (g_boss_state == BS_ENTER) { g_boss_state = BS_FIGHT; return; }
    if (g_boss_state == BS_DYING) {
        if (!(b_t & 3)) fx_spawn(FX(GN_CX - 32 + (rnd8() & 63)), FX(GN_CY - 32 + (rnd8() & 63)));
        if (++b_t > 120 && !old_n) g_boss_state = BS_DONE;
        return;
    }
    gn_a[0] += (u16)gn_spd;
    gn_a[1] -= (u16)(gn_spd >> 1);                            /* 短針は逆へ、半分の速さ(針が交差する) */
    gn_turn += (u16)(gn_spd < 0 ? -gn_spd : gn_spd);
    if (gn_turn >= 0x4000u) {                                 /* 一周: 速く。3 周目から逆回り */
        gn_turn -= 0x4000u; gn_lap++;
        gn_spd = (s16)(gn_spd + (gn_spd >> 1));
        if (gn_lap == 2) gn_spd = (s16)-gn_spd;
        if (gn_spd > 160) gn_spd = 160; else if (gn_spd < -160) gn_spd = -160;
    }
    /* 針に触れたら被弾(軸からの向きの成分と横の距離。桁あふれしないよう座標を半分に) */
    px = (s16)((PX(g_pl.x) + 8 - GN_CX) >> 1); py = (s16)((PX(g_pl.y) + 8 - GN_CY) >> 1);
    for (h = 0; h < gn_nh; h++) {
        s16 along, cross;
        gn_cs[h] = sin_i((u16)(gn_a[h] + 0x1000u)); gn_sn[h] = sin_i(gn_a[h]);
        along = (s16)(px * gn_cs[h] + py * gn_sn[h]); cross = (s16)(px * gn_sn[h] - py * gn_cs[h]);
        if (g_pl.alive && along > 0 && along < (s16)(gn_len[h] >> 1) * 127 && cross > -3 * 127 && cross < 3 * 127) player_hit();
    }
    /* 根元: 弱点。針の向きへ撃つ */
    g_boss_aim = GN_CY;
    if (hg_hit) hg_hit--;
    if (shot_rect(GN_CX - 9, GN_CY - 9, 18, 18)) {
        hg_hit = 4;                                           /* 当たった印(火花は b6_draw) */
        if (g_boss_hp) g_boss_hp--;
        if (!g_boss_hp) { g_boss_state = BS_DYING; b_t = 0; score_add(6000); return; }
    }
    if (!(t & 15)) enemy_bullet(FX(GN_CX + (gn_cs[0] >> 3)), FX(GN_CY + (gn_sn[0] >> 3)), (s16)(gn_cs[0] >> 2), (s16)(gn_sn[0] >> 2));
}
static u8 gn_inside(s16 r, s8 cs, s8 sn) {
    s16 x = (s16)(GN_CX + ((r * cs) >> 7)), y = (s16)(GN_CY + ((r * sn) >> 7));
    return (u8)(x >= 0 && x <= 255 && y >= GN_Y0 && y <= GN_Y1);
}
static void gn_hand(u8 h) {        /* 針 h を平行線で(長針 2 本・短針 3 本。画面の中で切る)。★長針を 220 ドットにして 3 本のままだと、
                                      試験 6(長針と短針)で LINE の命令が増えて 1 周の遅れが上限を超えた */
    u8 k;
    s8 cs = gn_cs[h], sn = gn_sn[h];
    s16 lo = 0, hi = gn_len[h], ex, ey;
    while (hi - lo > 1) { s16 m = (s16)((lo + hi) >> 1); if (gn_inside(m, cs, sn)) lo = m; else hi = m; }
    ex = (s16)((lo * cs) >> 7); ey = (s16)((lo * sn) >> 7);
    for (k = h ? 0 : 1; k < 3; k++) {
        s8 o = (s8)(k - 1);
        s16 x0 = GN_CX, y0 = GN_CY;
        if ((cs < 0 ? -cs : cs) > (sn < 0 ? -sn : sn)) y0 += o; else x0 += o;
        if (y0 + ey < GN_Y0 || y0 + ey > GN_Y1 || x0 + ex < 0 || x0 + ex > 255) continue;
        gq_line((u16)x0, (u16)(y0 + 68), ex, ey, h ? 13 : 7);     /* リングの行 = 画面の行 + 68(ページ 0)。★長針は明るい色 7(色 2 は
                                                                     紺の空にほぼ溶けて、触れると死ぬ針が見えなかった) */
        old_x[old_n] = x0; old_y[old_n] = y0; old_dx[old_n] = ex; old_dy[old_n] = ey; old_n++;
    }
}
static void gn_vram(void) {        /* 2 フレームに 1 回(針が 2 本なら 4 フレーム): 前の針を空の色で消し、今の針を描き、根元を描き直す */
    u8 k;
    if ((t & (gn_nh > 1 ? 3 : 1)) || gq_free() < 16) return;   /* ★針が 2 本(再校正版)は 4 フレームごと(試験 6 で遅れた) */
    for (k = 0; k < old_n; k++) gq_line((u16)old_x[k], (u16)(old_y[k] + 68), old_dx[k], old_dy[k], 1);
    old_n = 0;
    if (g_boss_state == BS_DYING || g_boss_state == BS_DONE || g_boss_state == BS_NONE) {
        if (g_boss_state == BS_DYING && b_t > 100) ring_cmd(0, 0, GN_CX - 10, GN_CY - 10, 20, 20, 1, 0x11);
        return;
    }
    for (k = 0; k < gn_nh; k++) gn_hand(k);
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
    if (g_sec == SEC6_MID) sand_update();
    if (g_sec == SEC6_BOSS && g_boss_state != BS_NONE && g_boss_state != BS_DONE) gn_update();
}

void b6_draw(void) {
    if (g_sec == SEC6_BOSS && hg_hit) spr_put(GN_CX - 8, GN_CY - 8, PAT_EXPL1, G_MISC, CS_EXPL);   /* 根元に当たった火花 */
    if (g_sec == SEC6_MID) sand_draw();
    if (g_sec == SEC6_MID && hg_hit) spr_put(HG_X + HG_N / 2 - 8, (s16)(s6_sy(HG_S + HG_N / 2) - 8), PAT_EXPL1, G_MISC, CS_EXPL);   /* 括れに当たった火花 */
    if (g_sec == SEC6_WALL || g_sec == SEC6_SHADOW) mite_draw();   /* ★影の中の敵を目だけにする演出はやめた(敵が消えたように見えた。遊んでの指摘) */
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
