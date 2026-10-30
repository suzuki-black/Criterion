/* boss.c — 1 面の中ボス LEGION と ボス PLUMB(指示書 5.1)。
   ・LEGION: 地上の Pawn(BG の 4x4、pawn.c)が地面から這い上がって、12 x 26 マスの人型に積み上がる(約 170 体)。
     胸に核(隊長機、スプライト)。撃たれたマスの Pawn は剥がれて落ち、しばらく待って這い上がって穴を埋める。
     核を壊すと上から崩れ、Pawn は地面へ落ちて左右へ逃げ散る。
     ★動いている Pawn(粒子)は最大 NPART 体。2 フレームに 1 回 1 マス(4 ドット)動き、そのたびに前のマスを
       「積んであれば Pawn、なければ空」で描き戻す。人型の揺れは、人型の行の帯ごとにラスターで横位置を
       ずらして見せる(VRAM の描き直し 0。広場の背景は空と街路で一様なので背景のずれは見えない)。
   ・PLUMB: 上空から鎖(スプライト 12 節)で吊られた直径約 58 ドット(64x64 の枠いっぱい)の真鍮の下げ振り。BG に描く。
     振り子で揺れる傾きは ROTOR(rotor.c)で前もって作った 7 枚(±34 度)から選ぶ。自転は胴の経線の縞
     (色 2..5)のパレットを回して見せる。体力が半分を切ると鎖が切れ、縮小した 3 枚(遠近)を使って跳ね回り、
     自転が速くなる。上部の吊り輪は 2 倍の傷。
     ★絵は都市後半に入った時点から ROTOR で 1 枚ずつ作り、ページ 2 に置く(boss_prepare / boss_vram_gen。
       1 フレームに 4 行の計算か 8 行の転送。10 枚で約 4 秒)。戦う間は 2 フレームに 1 回、ページ 2 の枠を表示リングへ
       HMMM で写し、前の位置からはみ出した分を空の色で塗る(GHOST の待ち行列)。広場の背景は空(色 1)なので
       枠の地も色 1 にして重ね描きする。雲の底(画面の 14 行より上)と歩道(158 行から)には描かない。
   座標はドット(中心)。画面の外の上は y < 0。 */
#include "s1.h"
#include "sys.h"
#include "spr.h"
#include "sprite_data.h"
#include "vdp.h"
#include "rom.h"
#include "rotor.h"
#include "ghost.h"
#include "assets.h"

#define LEGION_HP 40
#define PLUMB_HP  70
#define PLUMB_HALF 35

static u8  kind;
static u16 t;
/* 下げ振り 1 個ぶんの状態(7 面の再校正版は 2 個を入れ替えて動かすので 1 つにまとめる)。
   dr_*: 表示リングに描いてある枠(画面の座標、切り詰めた後)。w = 0 で無し。
   sh_*: 最後に写した絵の中心と枠(鎖はこれの吊り輪へつなぐ。★絵は 30Hz で写し、傾きは 7 枚に丸め、x は偶数に寄せるので、
         その時々の振れ角から吊り輪を求めると鎖の先が絵からずれて、ちょん切れて見えた)。sh_fr = 0xFF で無し。
   pd_*: 積んだがまだ VDP へ出ていない写し(pd_n = 1)。★待ち行列は混むと数フレーム遅れて出る(登場の直後で最もずれた)
         ので、鎖は写しが実際に出てから切り替える。出るまでは次の写しを積まない(積み重ねると覚えきれず、絵が先に
         進んで鎖が 1 つ前に残った) */
typedef struct {
    s16 bx, by, fx, fy, fvx, fvy, dr_x, dr_y, pivot, sh_x, sh_y, pd_x, pd_y;
    s8  swing;
    u8  broken, cur_fr, dr_w, dr_h, ph, sh_fr, pd_fr, pd_mk, pd_n;
} Plumb;
static Plumb P;
#define bx P.bx
#define by P.by
#define fx P.fx
#define fy P.fy
#define fvx P.fvx
#define fvy P.fvy
#define dr_x P.dr_x
#define dr_y P.dr_y
#define dr_w P.dr_w
#define dr_h P.dr_h
#define pivot P.pivot              /* 吊り元の x(普段は 128) */
#define swing P.swing
#define broken P.broken            /* 鎖が切れた(fx..fvy は切れてから。固定小数点) */
#define cur_fr P.cur_fr
#define ph P.ph                    /* 振れの位相 */
#define sh_x P.sh_x
#define sh_y P.sh_y
#define sh_fr P.sh_fr
#define pd_x P.pd_x
#define pd_y P.pd_y
#define pd_fr P.pd_fr
#define pd_mk P.pd_mk
#define pd_n P.pd_n

/* ---- LEGION ---- */
#define LG_W   12
#define LG_H   26
#define NPART  16
#define LG_X   152                 /* 人型の左端(画面。リングで 4 の倍数に寄せる) */
#define LG_Y   54                  /* 上端。最下段(25)の下端が歩道の上 157 */
/* 人型のマス(bit 11 = 左端の列) */
static const u16 lg_mask[LG_H] = {
    0x0F0, 0x1F8, 0x1F8, 0x0F0, 0x060, 0x3FC, 0x7FE, 0xFFF, 0xEF7, 0xCF3, 0xCF3, 0xCF3, 0xCF3,
    0x8F1, 0x0F0, 0x0F0, 0x1F8, 0x198, 0x198, 0x198, 0x198, 0x198, 0x198, 0x39C, 0x39C, 0x79E };
#define lg_on g_lg_on
#define gx0 g_lg_x0
static u8  asm_j, asm_i;           /* 組み上げの次のマス(下の行から) */
static u8  lg_left;                /* 積んであるマスの数 */
static u8  col_j;                  /* 崩落: 次に崩す行 */
enum { P_OFF, P_CLIMB, P_FALL, P_WAIT, P_RUN };
typedef struct { u8 mode, i, j, tj, t; s8 dir; } Part;
static Part part[NPART];
static u8  spread_t, aim_t;        /* 撃つ間隔(割り算・余りを毎フレーム使わない) */
static s8  sway[5];                /* 帯ごとの横のずれ(頭 → 脚)。人型の行 j の帯は j / 4(16 行から先は 4) */

/* ---- PLUMB ---- */
#define CHAIN_N 11                 /* ★12 だと他の 1 枚の敵と合わせて枠(11)からあふれ、鎖がちらついた */
#define CHAIN_L 96                 /* 吊り元から下げ振りの中心まで */
#define RING_D  25                 /* 中心から吊り輪まで */
#define NFRAME  10
#define FR_UP   3                  /* 直立・等倍 */
#define FR_FAR  7                  /* 縮小 3 枚の先頭 */
#define SKY_TOP 14                 /* 雲の底の下(これより上には描かない) */
#define SKY_BOT 158                /* 歩道(ここから下には描かない) */
#define FLOOR2  126                /* 鎖が切れてから跳ねる床(枠の下端 = SKY_BOT) */
/* 枠の並び(gen_boss.py の FRAMES と同じ): 傾き -6..6(64 で一周)× 等倍、直立の縮小 0.85 / 0.7 / 0.55 */
static const u8  fr_r[NFRAME]     = { 18, 18, 18, 18, 18, 18, 18, 15, 13, 10 };  /* 当たりの半径(C3: 胴の半径 29 に合わせた) */
static const u8  brass[4][3] = { { 3, 2, 1 }, { 5, 4, 1 }, { 7, 6, 3 }, { 5, 4, 1 } };
static s16 anchor_y;
static u8  spin;
static u8  gen_done;               /* 絵ができた(作るのは第 4 バンクの plumb_gen) */
/* 測定ビーム(指示書 5.1。★前は弾 3 発だった): 先端から歩道まで。1..24 = 赤い細い線の予告(当たらない)、25..44 = 白い太い線(当たる)、
   45 で空の色に塗って消す(広場の背景は空)。描いた位置に残る(振り子は振れ続ける)。2 個のときは 1 個目だけ */
static u8  bm_t, bm_dr;            /* 経過(0 = 無し)/ 描いてある段(0 = 無し、1 = 予告、2 = 発射) */
static s16 ch_x[CHAIN_N], ch_y[CHAIN_N], ch_px, ch_py;   /* 鎖の節の位置と、それを求めたときの絵の位置 */
static u8  ch_pf;
static s16 bm_x, bm_y;
/* 7 面の再校正版: 下げ振りが 2 個(吊り元を左右に分け、逆の位相で振って鎖を交差させる)。P を入れ替えて
   2 個ぶん動かす(体力は共有) */
static Plumb pl[2];
static u8 npl, cur_pl;
static void pl_cp(u8 *d, const u8 *src) { u8 n = sizeof(Plumb); while (n--) *d++ = *src++; }
static void pl_save(u8 i) { pl_cp((u8 *)&pl[i], (const u8 *)&P); }
static void pl_load(u8 i) { pl_cp((u8 *)&P, (const u8 *)&pl[i]); cur_pl = i; }
/* 場面の始め(game_start): 何も作っていない状態へ */
void boss_reset(void) {
    g_boss_state = BS_NONE; kind = 0xFF; gen_done = 1; bm_t = 0; bm_dr = 0; ch_pf = 0xFF; dr_w = 0; broken = 0;
}

void boss_start(u8 k) {
    u8 i;
    kind = k; t = 0; g_boss_state = BS_ENTER;
    if (k == BOSS_LEGION) {
        g_boss_hp = LEGION_HP; spread_t = 0; aim_t = 0; asm_j = LG_H - 1; asm_i = 0; lg_left = 0; gx0 = -1;
        for (i = 0; i < LG_H; i++) lg_on[i] = 0;
        for (i = 0; i < NPART; i++) part[i].mode = P_OFF;
        for (i = 0; i < 5; i++) sway[i] = 0;
    } else {
        g_boss_hp = PLUMB_HP; bx = 128; anchor_y = -110; broken = 0; aim_t = 0; spin = 0; dr_w = 0; sh_fr = 0xFF; pd_n = 0;
        cur_fr = FR_UP; swing = 0; pivot = 128; ph = 0; npl = 1; cur_pl = 0;
        if (RECAL) {                                        /* 再校正版: 2 個 */
            npl = 2;
            pivot = 100; ph = 0; pl_save(0);
            pivot = 156; ph = 32; pl_save(1);
            pl_load(0);
        }
    }
}

void boss_leave(void) { if (g_boss_state != BS_NONE) g_boss_state = BS_DONE; }

static void bdamage(u8 n) {
    if (g_boss_state != BS_FIGHT) return;
    if (g_boss_hp > n) { g_boss_hp -= n; return; }
    g_boss_hp = 0; g_boss_state = BS_DYING; t = 0;
    score_add(kind == BOSS_LEGION ? 200 : 500);
    if (kind == BOSS_LEGION) col_j = 0;                       /* 崩落は上の行から */
}

/* 自機のショット: 毎フレーム最初に画面座標(8 ビット)の表にしておき、部位ごとの判定は 8 ビットの比較だけにする
   (固定小数点のまま 10 部位 × 12 発を比べると、1 フレームに収まらなかった) */
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

/* (x, y) から半径 r(ドット)に当たった弾の数。当たった弾は消える(貫通は残る) */
static u8 shot_hit(s16 x, s16 y, u8 r) {
    u8 k, n = 0, r2 = (u8)(r * 2), ux, uy;
    if (x < 0 || x > 255 || y < 0 || y > 255) return 0;
    ux = (u8)(x - r); uy = (u8)(y - r);
    for (k = 0; k < nsh; k++) {
        Bullet *b;
        if ((u8)(shx[k] - ux) >= r2 || (u8)(shy[k] - uy) >= r2) continue;
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

/* 5 方向(左向き 2 ドット/フレーム) */
static void spread5(s16 x, s16 y) {
    enemy_bullet(FX(x), FX(y), -32, 0);
    enemy_bullet(FX(x), FX(y), -30, 10);  enemy_bullet(FX(x), FX(y), -30, -10);
    enemy_bullet(FX(x), FX(y), -26, 18);  enemy_bullet(FX(x), FX(y), -26, -18);
}

/* ---- LEGION のマス ---- */
static u8 band_of(u8 j) { return j < 16 ? (u8)(j >> 2) : 4; }
static s16 cell_x(u8 i, u8 j) { return (s16)(gx0 + i * 4 + sway[band_of(j)]); }   /* 画面での見た目の位置 */
static u8 is_on(u8 i, u8 j) { return (u8)((lg_on[j] >> (11 - i)) & 1); }
/* マスを描く(リングには揺れなしの位置で。揺れはラスターで見せる)。kind 0 / 1 = Pawn、2 = 空 */
static void cell_draw(u8 i, u8 j, u8 k) { pawn_cell((s16)(gx0 + i * 4), (s16)(LG_Y + j * 4), k); }

static Part *part_new(void) {
    u8 k;
    for (k = 0; k < NPART; k++) if (part[k].mode == P_OFF) return &part[k];
    return 0;
}

/* 粒子を 1 マス動かす。前のマスは積んであれば Pawn、なければ空で描き戻す。
   ★上下の 1 マスの移動は、動いた先と元のマスを 4x8 の 1 回の HMMM で済ませる(pawn_move) */
static void part_step(Part *p) {
    u8 oj = p->j, oi = p->i;
    switch (p->mode) {
    case P_CLIMB:
        if (p->j > p->tj) {
            p->j--;
            pawn_move((s16)(gx0 + oi * 4), (s16)(LG_Y + p->j * 4), is_on(oi, oj));
        }
        break;
    case P_FALL:
        if (p->j < LG_H - 1) {
            p->j++;
            pawn_move((s16)(gx0 + oi * 4), (s16)(LG_Y + oj * 4), (u8)(is_on(oi, oj) ? 1 : 2));
        } else { p->mode = p->tj == 0xFF ? P_RUN : P_WAIT; p->t = 0; }   /* tj = 0xFF: 崩落(戻らず逃げる) */
        break;
    case P_RUN:                                               /* 地面を左右へ逃げる */
        cell_draw(oi, oj, is_on(oi, oj) ? 0 : 2);
        if (++p->t > 12 || (p->dir < 0 ? p->i == 0 : p->i >= LG_W - 1)) { p->mode = P_OFF; return; }
        p->i = (u8)(p->i + p->dir);
        cell_draw(p->i, p->j, (u8)(p->t & 1));
        return;
    default:
        return;
    }
    if (p->mode == P_CLIMB && p->j == p->tj) {                /* 着いた: 積む(絵は A のまま) */
        lg_on[p->j] |= (u16)(1u << (11 - p->i)); lg_left++;
        p->mode = P_OFF;
    }
}

/* マスを剥がして落とす(粒子が足りなければ消すだけ) */
static void knock(u8 i, u8 j, u8 run) {
    Part *p = part_new();
    lg_on[j] &= (u16)~(1u << (11 - i)); lg_left--;
    if (!p) { cell_draw(i, j, 2); return; }
    p->mode = P_FALL; p->i = i; p->j = j; p->t = 0; p->tj = run ? 0xFF : j; p->dir = (rnd8() & 1) ? 1 : -1;
}

static void legion_update(void) {
    u8 k, i, j;
    if (g_boss_state == BS_ENTER) {
        if (g_scroll_speed) return;                            /* スクロールが止まってから */
        if (gx0 < 0) gx0 = (s16)(LG_X - (u8)((g_wx + LG_X) & 3));   /* リングで 4 の倍数に */
        /* 組み上げ: 下の行から順に、空いている粒子で地面から這い上がらせる(2 フレームに 1 体) */
        if (!(t & 1) && asm_j < LG_H) {
            while (asm_j < LG_H && !((lg_mask[asm_j] >> (11 - asm_i)) & 1)) {
                if (++asm_i >= LG_W) { asm_i = 0; asm_j--; }
            }
            if (asm_j < LG_H) {
                Part *p = part_new();
                if (p) {
                    p->mode = P_CLIMB; p->i = asm_i; p->j = LG_H - 1; p->tj = asm_j;
                    if (p->j == p->tj) { lg_on[asm_j] |= (u16)(1u << (11 - asm_i)); lg_left++; cell_draw(asm_i, asm_j, 0); p->mode = P_OFF; }
                    if (++asm_i >= LG_W) { asm_i = 0; asm_j--; }
                }
            }
        }
        if (asm_j >= LG_H) {                                  /* 全部出した: 粒子が着き終わったら戦う */
            for (k = 0; k < NPART && part[k].mode == P_OFF; k++) { }
            if (k == NPART) g_boss_state = BS_FIGHT;
        }
    } else if (g_boss_state == BS_DYING) {
        /* 戻る途中・待っている Pawn も逃げる */
        for (k = 0; k < NPART; k++) {
            Part *p = &part[k];
            if (p->mode == P_WAIT) { p->mode = P_RUN; p->t = 0; }
            else if (p->mode == P_CLIMB) { p->mode = P_FALL; p->tj = 0xFF; }
        }
        /* 上の行から崩す(1 フレームに 4 マスまで。1 行まとめると重いフレームになった) */
        if (col_j < LG_H) {
            u8 n = 0;
            for (i = 0; i < LG_W && n < 4 && gq_free() >= 8; i++) if (is_on(i, col_j)) { knock(i, col_j, 1); n++; }
            if (i == LG_W) col_j++;
        }
        if (!(t & 7)) fx_spawn(FX(gx0 + 8 + (rnd8() & 31)), FX(LG_Y + (rnd8() & 63) + (col_j << 2)));
        if (col_j >= LG_H) {
            for (k = 0; k < NPART && part[k].mode == P_OFF; k++) { }
            if (k == NPART) g_boss_state = BS_DONE;
        }
    } else {
        /* 戦う: 揺れ、撃つ、当たり */
        s8 w = g_sin64[(u8)(t >> 1) & 63];
        sway[0] = (s8)(w >> 5); sway[1] = (s8)(w >> 5); sway[2] = (s8)(w >> 6); sway[3] = (s8)(w >> 7); sway[4] = 0;
        for (k = 0; k < nsh; k++) {                            /* ショット → マス */
            s16 y = (s16)shy[k] - LG_Y, x;
            Bullet *b;
            if (y < 0 || y >= LG_H * 4) continue;
            j = (u8)(y >> 2);
            x = (s16)shx[k] - (gx0 + sway[band_of(j)]);
            if (x < 0 || x >= LG_W * 4) continue;
            i = (u8)(x >> 2);
            if (!is_on(i, j)) continue;
            if ((j >= 7 && j <= 9) && (i == 5 || i == 6)) continue;   /* 核の上は素通り(核が受ける) */
            b = &g_pshot[shi[k]];
            if (!b->on) continue;
            if (!b->pierce) b->on = 0;
            knock(i, j, 0);
            fx_spawn(FX(cell_x(i, j) + 2), FX(LG_Y + j * 4 + 2)); score_add(1);
        }
        {
            s16 cx = (s16)(gx0 + 24 + sway[2]), cy = LG_Y + 34;
            u8 n = shot_hit(cx, cy, 8);
            while (n--) bdamage(1);
            touch(cx, cy, 8);
            touch((s16)(gx0 + 24 + sway[1]), LG_Y + 20, 12);   /* 上半身に体当たり */
            if (++aim_t >= 64) {                              /* 腕(13 行の両端)が狙って撃つ */
                aim_t = 0;
                if (is_on(0, 13)) enemy_shoot(FX(cell_x(0, 13)), FX(LG_Y + 52));
                if (is_on(11, 13)) enemy_shoot(FX(cell_x(11, 13)), FX(LG_Y + 52));
            }
            if (++spread_t >= 150) { spread_t = 0; spread5(cx, cy); }
        }
        /* 落ちた Pawn: 待って、這い上がって穴を埋める */
        for (k = 0; k < NPART; k++) {
            Part *p = &part[k];
            if (p->mode == P_WAIT && ++p->t >= 60) { p->mode = P_CLIMB; }
        }
    }
    /* 粒子を動かす(半分ずつ、2 フレームに 1 回)。★GHOST の待ち行列に 1 歩分(最大 2 コマンド × 継ぎ目)の
       空きが無ければ、その粒子はこのフレームは動かさない(積めずに捨てると描き戻しが抜けて絵が崩れる) */
    for (k = (u8)(t & 1); k < NPART; k += 2) {
        Part *p = &part[k];
        if (p->mode != P_OFF && gq_free() >= 8) part_step(p);
    }
}

/* LEGION の揺れの帯(stage.c のラスターリストに入れる): 画面の行と横のずれ。戦っている間だけ */
u8 boss_bands(s16 *line, s8 *off) {
    u8 k;
    if (kind != BOSS_LEGION || g_boss_state != BS_FIGHT) return 0;
    for (k = 0; k < 5; k++) { line[k] = (s16)(LG_Y + k * 16); off[k] = sway[k]; }
    return 5;
}

static void plumb_pos(void) {
    s16 sx, cx;
    swing = (s8)(g_sin64[(u8)((t >> 1) + ph) & 63] >> 4);    /* 振れ角 ±7(64 で一周)≒ ±40 度 */
    sx = g_sin64[(u8)swing & 63]; cx = g_sin64[(u8)(swing + 16) & 63];
    bx = (s16)(pivot + ((sx * CHAIN_L) >> 7));
    by = (s16)(anchor_y + ((cx * CHAIN_L) >> 7));
    cur_fr = (u8)(FR_UP - ((swing + (swing < 0 ? -1 : 1)) >> 1));   /* 右へ振れたら吊り輪は左上(角度は逆) */
    if (cur_fr > 6) cur_fr = swing > 0 ? 0 : 6;
}

/* 吊り輪の位置(弱点)。中心 (x, y) の枠 fr の絵の中の吊り輪 = (x + rg_dx, y - rg_dy)。ROTOR は角度 a で時計回りに
   回すので (25 sin a, 25 cos a)、縮小の枠は 25 / 倍率(四捨五入。第 4 バンクの plumb_gen.c の fr_angle / fr_step と合わせる) */
static const s8 rg_dx[NFRAME] = { -14, -10, -5, 0, 5, 10, 14, 0, 0, 0 };
static const u8 rg_dy[NFRAME] = { 21, 23, 25, 25, 25, 23, 21, 21, 17, 14 };
#define rg_x(x, fr) ((s16)((x) + rg_dx[fr]))
#define rg_y(y, fr) ((s16)((y) - rg_dy[fr]))
static s16 ring_x(void) { return rg_x(bx, cur_fr); }
static s16 ring_y(void) { return rg_y(by, cur_fr); }

static void plumb_update(void) {
    if (g_boss_state == BS_ENTER) {
        if (!gen_done && t < 240) return;               /* ★絵ができるまで降りてこない(作り切るループで 0.25 秒止まった。4 秒で諦める) */
        if (cur_pl == 0) anchor_y += 2;                      /* 2 個のときも吊り元は 1 つの高さ */
        if (anchor_y >= -8) { anchor_y = -8; if (cur_pl == npl - 1) g_boss_state = BS_FIGHT; }
        plumb_pos();
        return;
    }
    if (g_boss_state == BS_DYING) {
        if (!(t & 3)) fx_spawn(FX(bx - 24 + (rnd8() & 47)), FX(by - 24 + (rnd8() & 47)));
        if (t >= 120) g_boss_state = BS_DONE;
        return;
    }
    if (!broken) {
        plumb_pos();
        if (g_boss_hp <= PLUMB_HALF) {                        /* 鎖が切れる: 跳ね回る */
            broken = 1; fx = FX(bx); fy = FX(by); fvx = bx < 128 ? 24 : -24; fvy = 0;
            fx_spawn(FX(pivot), FX(anchor_y + 40));
        }
        if (++aim_t >= 45) { aim_t = 0; enemy_shoot(FX(bx), FX(by + 20)); }
        if ((t & 127) == 64 && cur_pl == 0 && !bm_t) { bm_t = 1; bm_x = (s16)(bx & ~1); bm_y = (s16)(by + 24); }   /* 測定ビーム: 先端から下へ */
    } else {
        s8 d = g_sin64[(u8)(t >> 1) & 63];                    /* 遠近: 近い(等倍)〜遠い(0.55) */
        fx += fvx; fy += fvy; fvy += 2;
        if (fy > FX(FLOOR2)) {                                /* 跳ねる: 3 方向 */
            fy = FX(FLOOR2); fvy = -56;
            enemy_bullet(fx, fy, -32, -8); enemy_bullet(fx, fy, -28, -20); enemy_shoot(fx, fy);
        }
        if (fx < FX(40) || fx > FX(216)) fvx = (s16)-fvx;
        bx = PX(fx); by = PX(fy);
        cur_fr = d < 32 ? FR_UP : (u8)(FR_FAR + ((u8)(d - 32) >> 5));
    }
    {
        u8 n = shot_hit(bx, by + 2, fr_r[cur_fr]);
        u8 w = shot_hit(ring_x(), ring_y(), 5);               /* 吊り輪: 2 倍 */
        while (n--) bdamage(1);
        while (w--) bdamage(2);
        touch(bx, by + 2, (u8)(fr_r[cur_fr] - 2));
    }
    if (!(t & (broken ? 0 : 3))) {                            /* 自転: 縞のパレットを回す(切れたら速く) */
        u8 i;
        spin++;
        for (i = 0; i < 4; i++) {
            const u8 *c = brass[(u8)(i + spin) & 3];
            vdp_set_pal((u8)(2 + i), c[0], c[1], c[2]);
        }
    }
}

/* ---- PLUMB の絵: 作る(ROTOR → ページ 2)と、表示リングへ写す ---- */
#define SLOT_X(k) ((u16)(((k) & 3) * 64u))
#define SLOT_Y(k) ((u16)(512u + ((k) >> 2) * 64u))

void boss_prepare(u8 k) {
    if (k != BOSS_PLUMB) return;
    gen_done = plumb_gen(1);
}

/* 絵を 1 段ずつ作る(★第 4 バンクの plumb_gen。1 面のオーバレイの空きを作るため移した) */
void boss_vram_gen(void) { if (!gen_done) gen_done = plumb_gen(0); }

static void erase(s16 x, s16 y, s16 w, s16 h) {
    if (w > 0 && h > 0) ring_cmd(0, 0, x, y, (u8)w, (u8)h, 1, 0x11);
}

static void plumb_blit(u8 show) {
    s16 x = (s16)((bx - 32) & ~1), y = (s16)(by - 32), w = 64, h = 64, ox = 0, oy = 0;
    if (!show) { w = h = 0; sh_fr = 0xFF; pd_n = 0; }
    if (y < SKY_TOP) { oy = SKY_TOP - y; h -= oy; y = SKY_TOP; }
    if (y + h > SKY_BOT) h = SKY_BOT - y;
    if (x < 0) { ox = -x; w -= ox; x = 0; }
    if (x + w > 256) w = 256 - x;
    if (w <= 0 || h <= 0) w = h = 0;
    /* 前の枠のうち、新しい枠に入らない部分を空の色に(左右の帯と上下の帯) */
    if (dr_w) {
        s16 ox0 = dr_x, oy0 = dr_y, ox1 = dr_x + dr_w, oy1 = dr_y + dr_h;
        if (!w || x >= ox1 || x + w <= ox0 || y >= oy1 || y + h <= oy0) erase(ox0, oy0, dr_w, dr_h);
        else {
            s16 ix0 = x > ox0 ? x : ox0, ix1 = x + w < ox1 ? x + w : ox1;
            erase(ox0, oy0, ix0 - ox0, dr_h);                 /* 左 */
            erase(ix1, oy0, ox1 - ix1, dr_h);                 /* 右 */
            erase(ix0, oy0, ix1 - ix0, y - oy0);              /* 上 */
            erase(ix0, y + h, ix1 - ix0, oy1 - (y + h));      /* 下 */
        }
    }
    if (w) ring_cmd((u16)(SLOT_X(cur_fr) + ox), (u16)(SLOT_Y(cur_fr) + oy), x, y, (u8)w, (u8)h, 0, 0);
    dr_x = x; dr_y = y; dr_w = (u8)w; dr_h = (u8)h;
    if (show) {
        pd_n = 1; pd_x = (s16)(((bx - 32) & ~1) + 32); pd_y = by; pd_fr = cur_fr; pd_mk = (u8)(gq_done + gq_count);
    }
}
static void pd_settle(void) {      /* VDP へ出た写しを「写した絵」にする */
    if (pd_n && (s8)(gq_done - pd_mk) >= 0) { sh_x = pd_x; sh_y = pd_y; sh_fr = pd_fr; pd_n = 0; }
}

/* VRAM の窓で毎フレーム(stage.c): 絵を作る段と、PLUMB を写す */
extern u16 g_frame_t0;
void boss_vram(void) {
    {   /* ★重いフレームは次へ(強化を積んだ都市後半で遅れが続いた)。PLUMB は絵ができるまで降りてこないので、登場の間は
           締め切りを 200 ラインまで緩める(★2 フレームに 1 回締め切りを無視して作ると、ボスの声の PCM と重なって 3 フレーム続けて
           遅れた。重いフレームが続いても固まらないよう、plumb_update は 4 秒で待つのをやめる)(★間に合わない分を boss_start で作り切ると 0.25 秒止まった) */
        if (!gen_done && (u16)(sys_timer() - g_frame_t0) <= (g_boss_state == BS_ENTER ? 200u * 16u : 150u * 16u)) boss_vram_gen();
    }
    if (kind != BOSS_PLUMB || !gen_done) { gq_pump(0); return; }   /* 絵ができるまでは写さない */
    {   /* 測定ビーム: 段が替わったら、太い線の幅を空の色に塗り、次の回で描く。★PLUMB を写す偶数フレームを避け、残りが少なければ
           次へ(写しと同じフレームにまとめて出すと m5 で遅れた) */
        u8 w = !bm_t || bm_t > 44 ? 0 : bm_t > 24 ? 2 : 1;
        s16 h = (s16)(SKY_BOT - bm_y);
        if (w != bm_dr && (t & 1) && gq_free() >= 24 && h > 0 && (u16)(sys_timer() - g_frame_t0) < 150u * 16u) {
            if (bm_dr) { ring_cmd(0, 0, (s16)(bm_x - 2), bm_y, 6, (u8)h, 1, 0x11); bm_dr = 0; }
            else {
                if (w == 1) ring_cmd(0, 0, bm_x, bm_y, 2, (u8)h, 1, 0xDD);
                else ring_cmd(0, 0, (s16)(bm_x - 2), bm_y, 6, (u8)h, 1, 0xFF);
                bm_dr = w;
            }
        }
    }
    /* ★1 回の描き替えは最大 20 コマンド(写し 4 + 消し 4 方向 × リングの継ぎ目)。入りきらないときは見送る
       (途中まで積むと、消し残しや描き残しが出る) */
    /* ★残りが少ないフレーム(曲の小節の頭に FM の書き込みが集中した)は次のフレームへ(写しは 30Hz なので見た目は変わらない。m3 で 1 フレーム遅れた) */
    if (gq_free() >= 20 && (u16)(sys_timer() - g_frame_t0) < 170u * 16u) {
        u8 i = npl > 1 ? (u8)(t & 1) : 0;                     /* 2 個のときは 1 フレームおきに交互 */
        if (npl > 1) pl_load(i);
        if (g_boss_state == BS_DONE || g_boss_state == BS_NONE) {
            if (dr_w) plumb_blit(0);                          /* 最後に消す */
        } else if (npl > 1 || !(t & 1)) { pd_settle(); if (!pd_n) plumb_blit(1); }   /* 30Hz(前の写しが出てから) */
        if (npl > 1) pl_save(i);
    }
    gq_pump(0);                                               /* 待たない(メインループの数か所でも出す) */
}

void boss_update(void) {
    if (g_boss_dmg) { u8 d = g_boss_dmg; g_boss_dmg = 0; bdamage(d); }   /* LIMIT BREAK など(本体が預かった傷) */
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE) return;
    t++;
    shots_prepare();
    if (kind == BOSS_LEGION) { legion_update(); return; }
    if (bm_t && ++bm_t > 45) bm_t = 0;
    if (bm_t > 24 && g_pl.alive) {                            /* 発射の間: 自機の中心が線の幅に入り、先端より下なら */
        s16 dx = (s16)(PX(g_pl.x) + 8 - (bm_x + 1));
        if (dx > -4 && dx < 4 && PX(g_pl.y) + 8 > bm_y) player_hit();
    }
    if (npl == 1) plumb_update();
    else { u8 i; for (i = 0; i < npl; i++) { pl_load(i); plumb_update(); pl_save(i); } }
}

void boss_draw(void) {
    u8 i, k;
    if (g_boss_state == BS_NONE || g_boss_state == BS_DONE) return;
    if (kind == BOSS_LEGION) {
        if (g_boss_state == BS_FIGHT) spr_put((s16)(gx0 + 24 + sway[2]) - 8, LG_Y + 34 - 8, PAT_LEGION_CORE, G_ENEMY, 0);
        return;
    }
    for (k = 0; k < npl; k++) {
    if (npl > 1) pl_load(k);
    pd_settle();
    if (!broken && g_boss_state != BS_DYING && sh_fr != 0xFF) {   /* 吊り元から、写した絵の吊り輪まで 12 等分(1/12 ≒ 21/256) */
        /* ★位置は絵を写したとき(2 フレームに 1 回)しか変わらないので、1 個のときは計算を持ち越す(PLUMB の戦いは空きが少なく、
           曲の小節の頭に重なると 1 フレーム遅れた)。2 個のときは毎回求める */
        if (npl > 1 || sh_x != ch_px || sh_y != ch_py || sh_fr != ch_pf) {
            s16 rx = rg_x(sh_x, sh_fr), ry = rg_y(sh_y, sh_fr);
            s16 stx = (s16)((rx - pivot) * 21), sty = (s16)((ry - anchor_y) * 21), ax = stx, ay = sty;
            if (npl > 1) { stx <<= 1; sty <<= 1; ax = stx; ay = sty; }   /* 2 個のときは 1 つおきの節(スプライトの段が遅れた) */
            for (i = 0; i < (npl > 1 ? CHAIN_N / 2 : CHAIN_N); i++, ax += stx, ay += sty) {
                ch_x[i] = (s16)(pivot + (ax >> 8)) - 8; ch_y[i] = (s16)(anchor_y + (ay >> 8)) - 8;
            }
            ch_px = sh_x; ch_py = sh_y; ch_pf = sh_fr;
        }
        for (i = 0; i < (npl > 1 ? CHAIN_N / 2 : CHAIN_N); i++) spr_put(ch_x[i], ch_y[i], PAT_CHAIN, G_ENEMY, 0);
    }
    }
}
