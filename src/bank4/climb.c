/* climb.c — 1 面のビルの壁を登る Pawn(指示書 5.1「建物をよじ登る」。第 4 バンク。2026-10-29)。
   街路の Pawn(オーバレイの pawn.c)がビルの左の壁の列に来ると、ときどき壁の前へ移って登り始める(同時に 1 体)。4 フレームに
   2 ドット登り、屋上で止まって自機を撃つ。1 歩ごとに前の位置を背景へ描き戻し(stage_restore)、色 0 を透かして描く(gq_lmmm_t。
   壁の上なので、街路のように黒い余白で消せない)。撃てば倒せる。
   ★オーバレイ(1 面は満杯)に置くと 900 バイト近くになり入らなかったので、第 4 バンクへ。呼べるのはページ 0・1 のものだけ
   (stage_restore はそのためページ 1 の restore.c)。列の短冊は地図の変数(ページ 3)から自分で求める */
#include "game.h"
#include "ghost.h"
#define CITY_CLIMB
#include "city_data.h"

#define SHEET_Y  (512u + 200u)     /* pawn.c と同じ(Pawn の絵) */
#define WALK_X   48

static u16 cl_wx;
static u8  cl_s, cl_roof, cl_st;   /* cl_st: 0 = いない、1 = 登っている、2 = 屋上。★バンクの変数は初期化されないので pawn_cl_reset で */

void pawn_cl_reset(void) { cl_st = 0; }

static void cl_off(u8 boom) {
    s16 x = (s16)(cl_wx - g_wx);
    cl_st = 0;
    if ((u16)x > 250) return;
    stage_restore(x, (s16)(cl_s - g_scam), 6, 8);
    if (boom) { fx_spawn(FX(x + 3), FX(cl_s - g_scam + 4)); score_add(5); }
}

/* 街路の Pawn(世界の x = wx)がビルの左の壁の列にいれば登らせる。1 = 登らせた(呼んだ側が街路の絵を消して外す) */
u8 pawn_cl_try(u16 wx) {
    u16 m;
    u8 r;
    if (cl_st || (wx & 7) != 2 || rnd8() >= 128) return 0;
    m = (u16)((wx >> 3) - g_w0);
    if (m >= g_maplen) return 0;
    r = city_climb[g_strip_xlat[g_map[m] & 31] & 31];
    if (!r) return 0;
    cl_wx = wx; cl_s = 150; cl_roof = r; cl_st = 1;
    return 1;
}

/* 2 フレームに 1 回(pawn_update から。tick は pawn.c の数) */
void pawn_cl_update(u8 tick) {
    s16 x = (s16)(cl_wx - g_wx), y = (s16)(cl_s - g_scam);
    u8 k;
    if (!cl_st) return;
    if (x < -8 || x > 300) { cl_st = 0; return; }             /* 画面の外(列は流れで描き直される) */
    if (g_lb) { cl_off(1); return; }
    for (k = 0; k < MAX_PSHOT; k++) {                         /* ショット(1 回に 16 ドット進むので後ろへ広げる) */
        Bullet *b = &g_pshot[k];
        if (b->on && (u16)(PX(b->x) + 16 - x) < 24 && (u16)(PX(b->y) + 4 - y) < 14) {
            if (!(b->pierce | (b->on & 2))) b->on = 0;
            cl_off(1);
            return;
        }
    }
    if (cl_st == 1 && (tick & 1) && gq_free() >= 8 && (u16)x <= 250) {
        u16 rx;
        u8 ry;
        stage_restore(x, y, 6, 8);
        cl_s -= 2;
        if ((u8)(cl_s + 8) <= cl_roof) { cl_s = (u8)(cl_roof - 8); cl_st = 2; }
        rx = cl_wx & 511u; ry = (u8)(cl_s - g_scam + g_vscroll);
        if (ry <= 248) gq_lmmm_t((u16)(WALK_X + 2 + ((cl_s & 2) ? 10 : 0)), SHEET_Y, rx & 255u, (u16)((rx & 256u) + ry), 6, 8);
    }
    if (cl_st == 2 && !(tick & 31) && x > 32 && x < 224) enemy_shoot(FX(x + 3), FX(y));   /* 屋上から自機を狙う */
}
