/* s6boss.c — 6 面の歯車・砂粒虫・数字の砲台、中ボス HOURGLASS、ボス GNOMON(指示書 5.6)。
   ・歯車(ROTOR で回す)のコマを面の入りで作ってページ 3(68 行から下: 6 面の短冊は 0..67 行しか使わない)に置き、HMMM で
     表示リングへ写す(歯車 8 コマ = 45°(歯 8 枚)で一周、30fps)。砂時計は ROM から直に書く(hg_vram)。
   ・砂粒虫: 外壁の棚のスプライト。砂粒(落ちる弾)をこぼす。影の中では敵の体の色(12)が暗くなり、目(弾の色 8 の
     スプライト)だけが光る。
   ・HOURGLASS: 96x96(C3)。2 秒止まっては 180° 回り(1.5° ずつ 120 コマを 5 フレームずつ、10 秒)、横を向いている間だけ括れ
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
/* 0..7 歯車(32x32、元の半分の大きさ。ROTOR)。砂時計(96x96)は gen_stage6.py が 120 の角度(0..178.5°、1.5° ずつ)を描いて ROM に置き
   (0° は丸ごと、ほかは 2 つ前のコマとの差分)、回るたびに ROM から見えない半分へ直に書く(hg_vram)。★9 コマを 48x16 の片にして
   VRAM の空き(ページ 2・3、リングの見えない行)に置いていた間はそれ以上増やせず、18 コマを丸ごと書いても 1 コマ 12 フレームかかって、
   回っているように見えなかった(遊んでの指摘)。36 コマ(5° ずつ)でも縁が 1 回に 4 ドット飛んで、まだ回って見えなかった */
#define HG_N    96
#define HG_NF   S6_HG_NF           /* 砂時計のコマ */
#define RJ_END  8
extern u16 g_frame_t0;             /* main.c: このフレームの始まり */
static u8 rj, rph, rrow;           /* 仕事の番号 / 0 = 元を読む、1 = 回す、2 = VRAM へ / 進んだ行 */
static void rot_update(void) {
    if (rj >= RJ_END || g_sec < SEC6_SINK || g_calib) return;   /* ★試験 06 は歯車も砂時計も出ない(作るとリングの見える行に書いた) */
    if (rph == 0) {
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
static void rot_vram(void) {       /* 出来たコマを 8 行ずつ VRAM へ(重いフレームは次へ) */
    u8 k;
    if (rj >= RJ_END || rph != 2 || (u16)(sys_timer() - g_frame_t0) >= 150u * 16u) return;
    for (k = 8; k && rrow < 32; k--, rrow++) {
        vdp_write_addr17(1, (u16)(0x8000u + (u16)(68 + rrow) * 128u + rj * 16u));            /* ページ 3 の 68 行 */
        vdp_out_block(16, ROTOR_DST + (u16)rrow * 16u);
    }
    if (rrow >= 32) { rj++; rph = 0; }
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
static u8  hg_f, hg_shown, hg_chunk, hg_ph, hg_hit;   /* hg_chunk = 書いた行。★コマは見せてから進める(hg_update)ので、書いている途中では替わらない */
static u16 hg_t;
static u8  hg_j, hg_ok;            /* 今のコマを見せ始めた JIFFY / 1 = 次のコマを書き終えて見せる時刻を待っている
                                      (★処理落ちのフレームは hg_update が飛ぶので、フレームの数では間隔がばらついた) */
#define JIFFY (*(volatile u8 *)0xFC9E)
/* ★回る絵は二重にして写す(写している途中の、立っている砂時計と傾いた砂時計が混ざったコマが見えた。遊んでの指摘)。
   画面は止まっているので、リングの見えない半分(+256)にプレイエリアの写しを作り(hg_mir)、次のコマは見えていない方へ描いて、
   VDP が書き終えたら帯の横スクロールを 256 ずらして見せる(g_hg_disp。s6.c の ovl_bands)。BG に描くもの(砂の山・消すとき)は両方へ */
u8  g_hg_disp;                     /* 見せている半分(1 = +256) */
static u8  hg_mir, hg_q, hg_rd;    /* 写しの進み(0 = 写す、1 = 書き終えを待つ、HG_MIR = 済み) / 積んだ物の印(gq_done) / 戦いの後に描き直した列 */
#define HG_MIR 2
static u8  hg_first;               /* 1 = 入りの 1 回目の回転(コマ 1 は 0 → 1 の差分) */
static u16 hg_blk;                 /* 次に読む hg96.bin の塊 */
static u16 hg_mx;                  /* 写しの次の矩形(画面の x, y) */
static u8  hg_my;
#define HG_STEP 5                  /* 1 コマを見せる JIFFY(12Hz)。∞ の紋章のように一定の間隔で細かく替えると滑らかに見える。書くのに 3〜4 フレーム
                                      (VRAM の窓に間に合うのは処理落ちで論理を飛ばしたフレームだけで、ふつうのフレームは論理の後の 9.5ms から)。
                                      ★書き終えたらすぐ替えていた間は 3〜6 フレームでばらつき、回り方がぎくしゃくした */
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
        if (pile[c] < 24 && gq_free() >= 4) {
            ring_cmd(0, 0, (s16)(x & ~3), (s16)(fy - 2), 4, 2, 1, 0x77);
            ring_cmd(0, 0, (s16)((x & ~3) + 256), (s16)(fy - 2), 4, 2, 1, 0x77);   /* 見えない半分の写しにも */
            pile[c] += 2;
        }
    }
}
static void sand_draw(void) {
    u8 i;
    for (i = 0; i < NSAND; i++) if (sd_on[i]) spr_put(PX(sd_x[i]) - 8, PX(sd_y[i]) - 8, PAT_EBUL, G_EBUL, 0);
}

void b6_mid_start(void) { { u8 i; for (i = 0; i < NSAND; i++) sd_on[i] = 0; for (i = 0; i < 64; i++) pile[i] = 0; } hg_f = 0; hg_shown = 0xFF; hg_chunk = 0; hg_hit = 0; hg_ph = 0; hg_t = 0; g_hg_disp = 0; hg_mir = 0; hg_rd = 0; hg_mx = 0; hg_my = SPLIT_H; hg_first = 1; hg_blk = 0; hg_ok = 0; g_boss_state = BS_ENTER; g_boss_hp = 16; b_t = 0; }
static void hg_update(void) {
    s16 cy = s6_sy(HG_S + HG_N / 2), cx = HG_X + HG_N / 2;
    if (g_boss_state == BS_ENTER) { if (hg_mir >= HG_MIR) g_boss_state = BS_FIGHT; return; }
    if (g_boss_state == BS_DYING) {
        if (!(b_t & 3)) fx_spawn(FX(HG_X + (rnd8() & 63) + (rnd8() & 31)), FX(s6_sy(HG_S) + (rnd8() & 63) + (rnd8() & 31)));
        if (++b_t == 90) hg_f = 0xFE;                         /* 空で塗る(b6_vram) */
        if (b_t > 100 && hg_shown == 0xFE && hg_rd >= 32) g_boss_state = BS_DONE;   /* 見えない半分を描き直してから流す */
        return;
    }
    hg_t++;
    if (hg_hit) hg_hit--;
    if (hg_ph == 0) {                                         /* 止まっている: 括れから砂がこぼれ、上下の膨らみから自機を狙う */
        /* ★真下に砂を落とすだけだった(遊んでの指摘「ずっと真下に球を吐いている」)。砂は前半だけ、狙い撃ちと回り終わりの 8 方向を足す */
        if (hg_t < 64 && !(hg_t & 3)) sand_spawn((s16)(cx - 2 + (rnd8() & 3)), (s16)(cy + 4), (s8)((rnd8() & 7) - 4), FX(1), (u8)(cy + 26));   /* 括れから下の砂へこぼれる */
        if (hg_t == 30 || hg_t == 80) { enemy_shoot(FX(cx), FX(cy - 28)); enemy_shoot(FX(cx), FX(cy + 28)); }
        if (hg_t >= 120) { hg_ph = 1; hg_t = 0; hg_f = 1; hg_j = JIFFY; }
    } else {                                                  /* 180° 回る(コマ 1..HG_NF-1 → 0)。前のコマを見せたら次を書き始める
                                                                 (★時間で進めていた間は、写し終える前に次のコマになって途中が見えた) */
        u8 f = hg_f;
        if (hg_shown == f) {                                  /* 見せたら次のコマを書き始める(見せる間隔は hg_vram) */
            hg_t = 0;
            if (++f >= HG_NF) {                               /* 回り終わり: 括れから 8 方向へ砂を散らす */
                u8 k;
                f = 0; hg_ph = 0;
                for (k = 0; k < 8; k++) sand_spawn(cx, cy, (s8)(hg_ring[k][0] + (rnd8() & 15) - 8), hg_ring[k][1], 0);   /* 外へ飛んで床に積もる(向きを揺らして山をならす) */
            }
            hg_f = f;
        }
        if (f >= HG_NF * 8 / 18 && f <= HG_NF * 10 / 18) {    /* 括れ(弱点)が横を向いている(80°..100°。約 70 フレーム。★36 コマで 20 フレーム
                                                                 だった間は弾が届く前に閉じて、m5 で倒せなかった) */
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
/* プレイエリア(SPLIT_H..PLAY_H-1 行)を見えない半分へ写す 1 つ(64x16 ずつ。戻り値 1 = 写し終えた)。見えている半分の列 c と +256 の列は
   同じ x の別のページなので、リングの継ぎ目(写し元のページが替わる画面の x = xa、256 行を越える画面の y = ya)で切れば、写し元と
   写し先は同じ形の矩形になる。★1 つを大きくすると(256x140 で約 90ms)上の帯(SCREEN 12)の間までコマンドが続き、化けて片の置き場を壊した */
static u8 hg_mirror(void) {
    u16 xa = (u16)(256 - (g_wx & 255)), ya = (u16)((256 - g_vscroll) & 255);
    u16 x1 = (u16)((hg_mx & ~63u) + 64), y1 = (u16)((hg_my & ~15u) + 16), rx, ry;
    if (hg_mx < xa && x1 > xa) x1 = xa;
    if (hg_my < ya && y1 > ya) y1 = ya;
    if (y1 > PLAY_H) y1 = PLAY_H;
    rx = (u16)((g_wx + hg_mx) & 511u); ry = (u16)((hg_my + g_vscroll) & 255);
    gq_hmmm(rx & 255, rx >= 256 ? 256u + ry : ry, rx & 255, rx >= 256 ? ry : 256u + ry, x1 - hg_mx, y1 - hg_my);
    hg_mx = x1;
    if (hg_mx >= 256) { hg_mx = 0; hg_my = (u8)y1; }
    return (u8)(hg_my >= PLAY_H);
}
/* 砂時計を書く: 塊(256 バイト)を RAM に読み、記録ごとに(行・バイトの位置・長さ・中身)リングの半分 h へ書く(入りの 0° は 1 行丸ごとの
   記録、回る間は 2 つ前のコマとの差分)。リングの 256 / 512 列の継ぎ目で 2 つに分ける */
static u8  hg_pos, hg_sy0, hg_k, hg_kn, hg_r14;   /* hg_kn = 時刻を見る記録の間隔 */
static u16 hg_budget;               /* 書いてよいフレームの頭からの時刻(タイマのカウント) */
static u16 hg_rx0;                 /* 半分 h の砂時計の左端のリングの x(偶数) */
static u8  hg_buf[256];
static void hg_load(void) {        /* 塊 hg_blk を hg_buf へ */
    u16 off = (u16)(ASSET_HG96_OFF + ((hg_blk & 31) << 8));
    u8 bank = (u8)(ASSET_HG96_BANK + (hg_blk >> 5));
    if (off >= 8192) { off -= 8192; bank++; }
    rom_to_ram(bank, off, hg_buf, 256);
    hg_pos = 0;
}
/* hg_buf[hg_pos] から記録を書く(asm)。戻り値 0 = 時間切れ、0xFE = 塊の終わり、0xFF = コマの終わり(hg_pos はその印を指す)。
   ★C で書いていた間は 1 記録に約 200us(中身は平均 4 バイト = 約 35us)かかり、120 コマの差分 1 コマに 9 フレームかかった。
   番地は VDP の 2〜4 回の書き込みだけにし、時刻を見る(と PCM の送出口を呼ぶ)のは回る間は 4 記録に 1 回。
   VRAM の番地 = 半分(リングの x の 256 の位) << 15 | 行 << 7 | バイトの位置(R#14 = 番地 >> 14) */
#define HG_BUDGET_IN  (225u * 16u)  /* 入りの 0°: フレームの頭から 225 ライン(VRAM の窓は約 98 ラインから、上の帯(SCREEN 12)は 294 ラインから。
                                        ★240 では 0°(1 記録 48 バイト。4 記録ごとに時刻を見るので超えうる)を書く 6 フレームが遅れた(m5)) */
#define HG_BUDGET_ROT (225u * 16u)  /* 回る間の差分。★240 では処理落ち(敵と砂が止まる)が増えた。220 以下では書くのが 5 フレームに間に合わないコマが 3 分の 1 を超えた */
static u8 hg_run(void) __naked {
    __asm
        ld   a, (_hg_pos)
        ld   e, a
        ld   d, #0
        ld   hl, #_hg_buf
        add  hl, de
        ld   a, #1
        ld   (_hg_k), a
        ld   a, #0xFF
        ld   (_hg_r14), a          ; R#14 はほかの書き込みが替えるので、呼ぶたびに覚え直す
    00010$:
        ld   a, (hl)
        cp   #0xFE
        jp   nc, 00090$            ; 塊・コマの終わり
        ld   a, (_hg_k)
        dec  a
        ld   (_hg_k), a
        jr   nz, 00020$
        ld   a, (_hg_kn)
        ld   (_hg_k), a
        push hl
        ld   hl, (_g_vdp_wait_hook)
        ld   a, h
        or   l
        call nz, 00099$            ; PCM の送出口
        call _sys_timer            ; DE = 今
        ld   hl, (_g_frame_t0)
        ex   de, hl
        or   a
        sbc  hl, de
        ld   de, (_hg_budget)
        or   a
        sbc  hl, de
        pop  hl
        jr   c, 00020$
        xor  a                     ; 時間切れ
        jp   00090$
    00020$:
        ld   a, (_hg_sy0)
        add  a, (hl)
        ld   b, a                  ; B = リングの行
        inc  hl
        ld   a, (hl)               ; バイトの位置
        inc  hl
        add  a, a
        ld   e, a
        ld   a, #0
        adc  a, a
        ld   d, a                  ; DE = バイトの位置 * 2
        ld   c, (hl)               ; C = 長さ
        inc  hl                    ; HL = 中身
        push hl
        ld   hl, (_hg_rx0)
        add  hl, de
        ld   a, h
        and  #1
        ld   d, a                  ; D = 半分(ページ 0 / 1)
        ld   a, l
        srl  a
        ld   e, a                  ; E = その行のバイト
        pop  hl
        ld   a, #128
        sub  e                     ; A = ページの継ぎ目までのバイト
        cp   c
        jr   c, 00030$
        call 00080$
        ld   b, c
        ld   c, #0x98
        otir
        jp   00010$
    00030$:                        ; 継ぎ目をまたぐ: 2 つに分ける
        push af
        call 00080$
        pop  af
        push bc
        ld   b, a
        ld   c, #0x98
        otir
        pop  bc
        neg
        add  a, c
        ld   c, a                  ; 残り
        ld   a, d
        xor  #1
        ld   d, a
        ld   e, #0
        call 00080$
        ld   b, c
        ld   c, #0x98
        otir
        jp   00010$
    00080$:                        ; VRAM の番地(D = 半分、B = 行、E = バイト)。A だけ壊す
        ld   a, d
        add  a, a
        bit  7, b
        jr   z, 00081$
        inc  a
    00081$:
        push hl
        ld   hl, #_hg_r14
        di
        cp   (hl)
        jr   z, 00082$             ; ★同じなら R#14 を書かない(1 記録 4 回の I/O が 2 回になる)
        ld   (hl), a
        out  (0x99), a
        ld   a, #0x8E
        out  (0x99), a
    00082$:
        pop  hl
        ld   a, b
        rrca
        and  #0x80
        or   e
        out  (0x99), a
        ld   a, b
        srl  a
        and  #0x3F
        or   #0x40
        out  (0x99), a
        ei
        ret
    00090$:
        ld   b, a
        ld   de, #_hg_buf
        or   a
        sbc  hl, de
        ld   a, l
        ld   (_hg_pos), a
        ld   a, b
        ret
    00099$:
        jp   (hl)
    __endasm;
}
/* 塊 hg_blk から半分 h へ書く(フレームの頭から hg_budget まで)。1 = 書き終えた */
static u8 hg_write(u8 h) {
    if (hg_mir < HG_MIR) { hg_budget = HG_BUDGET_IN; hg_kn = 1; }   /* ★入りの 0° は 1 記録 48 バイトなので毎回見る(4 記録ごとでは 6 フレーム遅れた) */
    else { hg_budget = HG_BUDGET_ROT; hg_kn = 4; }
    hg_rx0 = (u16)((g_wx + HG_X + h * 256u) & 510u);
    hg_sy0 = (u8)(s6_sy(HG_S) + g_vscroll);
    if (!hg_chunk) { hg_load(); hg_chunk = 1; }
    {
        u16 t = sys_timer();                                  /* ★使い切る書き込みの時間は「重かった」に数えない(g_elastic) */
        u8 r;
        for (;;) {
            r = hg_run();
            if (r == 0xFF) { hg_blk++; hg_chunk = 0; r = 1; break; }
            if (r != 0xFE) { r = 0; break; }
            hg_blk++; hg_load();
        }
        g_elastic += (u16)(sys_timer() - t);
        return r;
    }
}
static void hg_vram(void) {        /* コマが替わったら見えない方へ書き、書き終えたら決まった時刻に次のフレームの帯から見せる */
    u8 f = hg_f, back;
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE || gq_free() < 8) return;
    if (f == 0xFE) {
        if (hg_shown != 0xFE) {
            ring_cmd(0, 0, HG_X, s6_sy(HG_S), HG_N, HG_N, 1, 0x11); ring_cmd(0, 0, HG_X + 256, s6_sy(HG_S), HG_N, HG_N, 1, 0x11);
            hg_shown = 0xFE; g_hg_disp = 0;
        } else if (hg_rd < 32 && stage_redraw_col((u16)((g_wx >> 3) + 32 + hg_rd))) hg_rd++;   /* 見えない半分の列を描き直す */
        return;
    }
    if (hg_mir < HG_MIR) {                                    /* 入り: 見えている方に 0° を書き(塊 0 から)、プレイエリアを見えない半分へ写す */
        if (hg_shown != 0) { if (hg_write(0)) hg_shown = 0; return; }
        if (stage_busy()) return;
        if (!hg_mir) { if (hg_mirror()) { hg_q = (u8)(gq_done + gq_count); hg_mir = 1; } }
        else if ((s8)(gq_done - hg_q) >= 0 && !vdp_cmd_busy()) hg_mir = HG_MIR;
        return;
    }
    back = (u8)(g_hg_disp ^ 1);
    if (hg_ok) {                                              /* 書き終えたコマは決まった間隔で見せる(★書き終えたらすぐ見せていた間は、
                                                                 書く時間の違いで 3〜6 フレームにばらついた) */
        if ((u8)(JIFFY - hg_j) < HG_STEP) return;
        hg_j = JIFFY;                                         /* ★遅れた分を次で取り戻すと 6 → 4 と揺れたので、遅れたらそこから数え直す */
        hg_ok = 0; hg_shown = f; g_hg_disp = back; if (f == 1) hg_first = 0;
        return;
    }
    if (!hg_chunk) {                                          /* 書き始め: 1 と 2 の頭は決まった塊、ほかは前のコマの続き */
        if (hg_shown == f) return;
        if (f == 1) hg_blk = hg_first ? S6_HG_FIRST : S6_HG_D1;
        else if (f == 2) hg_blk = S6_HG_D2;
    }
    if (hg_write(back)) hg_ok = 1;
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
