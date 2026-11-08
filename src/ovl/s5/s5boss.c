/* s5boss.c — 5 面の砲台、中ボス HANGAR DECK、ボス GAUGE BLOCK(指示書 5.5)。
   ・機銃(艦首・甲板): スプライト。自機の方へ 8 向き(上の半円、プリレンダ)で旋回して撃つ。
   ・主砲塔(甲板 4 基): 台は短冊(BG)、上の 32x32 は 16 向き(左 → 真上 → 右)をプリレンダしてページ 2 に置き、
     向きが変わったときだけ表示リングへ HMMM で写す(GHOST)。壊すと砲身が吹き飛ぶ(空で塗る)。
   ・吊り砲台(艦底): スプライト 3 向き。撃つ瞬間に光る。
   ・HANGAR DECK: 扉の列を地図の書き換えで開け、エレベーター(BG の板)が昇って Notch を 1 機ずつ射出する。
     板を壊すと格納庫が誘爆し、扉の列が穴になる(そこから艦底へ回り込む)。
   ・GAUGE BLOCK: 艦橋の上の 6 段のブロック(長さ 32..112)。段ごとに帯を分け、2 段ずつ帯の横位置で画面の外へ
     すべらせ、見えない間に描き替えて戻る(段の入れ替え)。弱点は一番短いブロック(入れ替えのたびに場所が変わる)。 */
#include "s5.h"
#include "spr.h"
#include "sprite_data.h"
#include "vdp.h"
#include "rom.h"
#include "ghost.h"
#include "assets.h"
#include "s5_data.h"
#include "sys.h"

extern u16 g_frame_t0;

#define PAT_AA    44               /* 機銃 8 向き(44..51)。3 面の針の枠を使う */
#define PAT_HANG  60               /* 吊り砲台 3 向き(60..62) */

static u16 t;
static u8  spr_loaded, sheet_row, gauge_ld;

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
/* 矩形 (x, y, w, h) に入った弾を消して数を返す */
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

/* 向きの表(×32): 機銃は 8 向き、主砲塔は 16 向き。どちらも左(180°)→ 右(0°)の上の半円 */
static const s8 aa_ux[8] = { -32, -29, -20, -7, 7, 20, 29, 32 };
static const s8 aa_uy[8] = { 0, -14, -25, -31, -31, -25, -14, 0 };
static const s8 tu_ux[16] = { -32, -32, -30, -28, -24, -20, -14, -8, -3, 3, 8, 14, 20, 24, 28, 30 };
static const s8 tu_uy[16] = { 0, -7, -13, -19, -23, -27, -30, -31, -32, -32, -31, -30, -27, -23, -19, -13 };
/* (dx, dy) に一番近い向き(内積が最大)。n 向き */
static u8 best_dir(s16 dx, s16 dy, const s8 *ux, const s8 *uy, u8 n) {
    u8 i, b = 0;
    s16 best = -32767;
    s8 ax = (s8)(dx >> 2), ay = (s8)(dy >> 2);                /* 掛け算の桁を抑える。★8 ビット同士にする(画面の差は ±280 なので
                                                                 >> 2 で収まる。16 ビットの掛け算を主砲塔 1 基で 32 回して、甲板で遅れた) */
    for (i = 0; i < n; i++) {
        s16 d = (s16)(ax * ux[i] + ay * uy[i]);
        if (d > best) { best = d; b = i; }
    }
    return b;
}
static s16 plx(void) { return PX(g_pl.x) + 8; }
static s16 ply(void) { return PX(g_pl.y) + 8; }

/* ======== 機銃 ======== */
static u8 aa_hp[S5_NAA], aa_dir[S5_NAA];
static void aa_update(void) {
    u8 i;
    for (i = 0; i < S5_NAA; i++) {
        s16 x = s5_sx(s5_aa[i]) + 4, y = s5_sy(S5_DECK - 4) - 8;
        if (!aa_hp[i] || x < -8 || x > 264) continue;
        if (!((t + i) & 15)) aa_dir[i] = best_dir(plx() - x, ply() - y, aa_ux, aa_uy, 8);   /* ★16 フレームに 1 回(1 フレームに 1 門)。8 では 2 門が
                                                                 主砲塔と重なり、BGM の山と合わせて甲板で遅れた */
        if ((LB_NOW ? (aa_hp[i] = 1) : shot_rect(x - 7, y - 7, 14, 14)) && --aa_hp[i] == 0) { fx_spawn(FX(x), FX(y)); score_add(30); continue; }
        /* ★画面の中にいるときだけ撃つ(縦を見ていなかったので、船体の下へ潜った後も画面の上の外の甲板から弾が降ってきた。遊んでの指摘) */
        if (((t + i * 29) & 127) == 0 && x > 16 && x < 240 && y > 8 && y < PLAY_H)
            enemy_bullet(FX(x), FX(y), (s16)(aa_ux[aa_dir[i]] << 1), (s16)(aa_uy[aa_dir[i]] << 1));
    }
}
static void aa_draw(void) {
    u8 i;
    for (i = 0; i < S5_NAA; i++) {
        s16 x = s5_sx(s5_aa[i]) + 4;
        if (!aa_hp[i] || x < -8 || x > 264) continue;
        spr_put(x - 8, (s16)(s5_sy(S5_DECK - 4) - 16), (u8)(PAT_AA + aa_dir[i]), G_ENEMY, 0);
    }
}

/* ======== 主砲塔 ======== */
#define TUR_S 4                    /* 上部の絵の上端の行(s)。0..3 はブロック 23(全列で共通)なので避ける */
static u8 tu_hp[S5_NTUR], tu_dir[S5_NTUR], tu_shown[S5_NTUR], tu_want[S5_NTUR];
static void tur_update(void) {
    u8 i;
    for (i = 0; i < S5_NTUR; i++) {
        s16 x = s5_sx(s5_tur[i]), cx = x + 16, cy = s5_sy(TUR_S + 22);
        if (!tu_hp[i] || x < -32 || x > 256) continue;
        if (!((t + i * 3) & 7)) tu_want[i] = best_dir(plx() - cx, ply() - cy, tu_ux, tu_uy, 16);
        if (!((t + i) & 7) && tu_dir[i] != tu_want[i]) tu_dir[i] += tu_dir[i] < tu_want[i] ? 1 : -1;   /* ゆっくり旋回 */
        if ((LB_NOW ? (tu_hp[i] = 1) : shot_rect(x + 4, cy - 14, 24, 22)) && --tu_hp[i] == 0) {
            fx_spawn(FX(cx), FX(cy)); fx_spawn(FX(cx - 10), FX(cy - 8)); fx_spawn(FX(cx + 10), FX(cy - 12));
            score_add(500);
            tu_shown[i] = 0xFE;                               /* 砲身を吹き飛ばす(空で塗る) */
            continue;
        }
        if (((t + i * 37) & 63) == 0 && x > 0 && x < 224 && cy > 8 && cy < PLAY_H)   /* 画面の中だけ(機銃と同じ) */
            enemy_bullet(FX(cx + (tu_ux[tu_dir[i]] >> 1)), FX(cy + (tu_uy[tu_dir[i]] >> 1)),
                         (s16)(tu_ux[tu_dir[i]] * 3 >> 1), (s16)(tu_uy[tu_dir[i]] * 3 >> 1));
    }
}
/* 向きが変わった砲塔を描く(1 フレーム 1 基。台の列が描き終わってから) */
static void tur_vram(void) {
    u8 i;
    for (i = 0; i < S5_NTUR; i++) {
        s16 x = s5_sx(s5_tur[i]);
        u8 d;
        if (x < -32 || x > 320 || gq_free() < 6) continue;
        if (!stage_col_ready((u16)(g_w0 + s5_tur[i])) || !stage_col_ready((u16)(g_w0 + s5_tur[i] + 3))) continue;
        if (tu_shown[i] == 0xFE) {                            /* 壊れた: 上部を空に */
            ring_cmd(0, 0, x, s5_sy(TUR_S), 32, 26, 1, 0x11);
            tu_shown[i] = 0xFD;
            return;
        }
        if (!tu_hp[i]) continue;
        d = tu_dir[i];
        if (tu_shown[i] == d) continue;
        ring_cmd((u16)((d & 7) * 32u), (u16)(512u + (d >> 3) * 32u), x, s5_sy(TUR_S), 32, 32, 0, 0);
        tu_shown[i] = d;
        return;
    }
}

/* ======== 吊り砲台(艦底) ======== */
static u8 hg_hp[S5_NHANG], hg_dir[S5_NHANG];
static s16 dc_x, dc_y;             /* 吊り砲台が撃った所(光らせる) */
static u8  dc_t;
static void hang_update(void) {
    u8 i;
    for (i = 0; i < S5_NHANG; i++) {
        s16 x = s5_sx(s5_hang[i]) + 4, y = s5_sy(S5_HULL_B + 4) + 6;
        s16 dx;
        if (!hg_hp[i] || x < -8 || x > 264) continue;
        dx = plx() - x;
        hg_dir[i] = dx < -24 ? 0 : dx > 24 ? 2 : 1;
        if ((LB_NOW ? (hg_hp[i] = 1) : shot_rect(x - 7, y - 7, 14, 14)) && --hg_hp[i] == 0) { fx_spawn(FX(x), FX(y)); score_add(50); continue; }
        if (((t + i * 23) & 63) == 0 && x > 16 && x < 240) {
            enemy_bullet(FX(x), FX(y + 4), (s16)((hg_dir[i] - 1) * 24), 36);
            dc_x = x; dc_y = y; dc_t = 8;                     /* ★撃つ瞬間に砲台を光らせる(小さくて目立たず、どこから撃ったか分からなかった) */
        }
    }
    if (dc_t) dc_t--;
    /* ★艦底から落とす爆雷は外した(横位置がでたらめで、元の敵が見えない弾になった。遊んでの指摘「敵がいないのに敵弾が発射される」) */
}
static void hang_draw(void) {
    u8 i;
    if (dc_t) spr_put(dc_x - 8, dc_y - 8, PAT_EXPL1, G_MISC, CS_EXPL);   /* 吊り砲台の撃った光 */
    for (i = 0; i < S5_NHANG; i++) {
        s16 x = s5_sx(s5_hang[i]) + 4;
        if (!hg_hp[i] || x < -8 || x > 264) continue;
        spr_put(x - 8, s5_sy(S5_HULL_B + 4) - 2, (u8)(PAT_HANG + hg_dir[i]), G_ENEMY, 0);
    }
}

/* ======== HANGAR DECK ======== */
/* ★遊んでの指摘(何を壊せばいいのかわからない)で作り直した: 板(48x4 の細い棒)を、危険の縞と赤いランプの付いた台(48x12)にし、
   Notch を載せて昇る。当たると台が白く光り(絵を白い版に替える。8..15 はスプライトと共有なのでパレットでは光らせない)、
   体力が減るほどランプの点滅が速くなって火花が出る。上で止まる時間は 0.5 → 1.5 秒。扉が開くときに帯で TARGET を出す */
#define LIFT_W   48
#define LIFT_H   12
#define LIFT_MAX 44                /* 台の上端は格納庫の底(s 60。台の下端が床の 72)から甲板の上(s 16)まで(★甲板より下は自機の弾が届かない) */
#define LIFT_IMG 128               /* ページ 2 の 128..139 行・x 0..47 に台の絵(白い点滅とランプの消灯は上から塗る) */
static u8 door_n;                  /* 書き換えた扉の列 */
static u8 door_w;                  /* 開いた幅(中央から片側。0..32 ドット) */
static u8 lift, lift_drawn, lift_ph, lift_wait, lift_v, lift_vd, lift_img, lift_fl, lamp_t;
static u8 boss_t;
static s16 lift_s(void) { return (s16)(S5_DECK + 20 - lift); }
void b5_hangar_start(void) {
    door_n = 0; door_w = 0; lift = 0; lift_drawn = 0xFF; lift_ph = 0; lift_wait = 60; lift_vd = 0xFF; lift_img = 0; lift_fl = 0; lamp_t = 0;
    g_boss_state = BS_ENTER; g_boss_hp = 30; boss_t = 0;
    stage_banner_c("TARGET: ELEVATOR", 13);
}
/* 台の絵のドット */
static u8 lift_px(u8 x, u8 y) {
    if (y == 0) return 14;
    if (y == LIFT_H - 1) return 2;
    if (y <= 2 || y >= LIFT_H - 3) return (u8)((((x + y) >> 2) & 1) ? 12 : 0);   /* 危険の縞(黄と黒) */
    if (x < 2 || x >= LIFT_W - 2) return 14;
    if (x >= 18 && x < 30 && y >= 4 && y <= 7) return (u8)(y == 4 && x < 22 ? 15 : 13);   /* ランプ */
    if (y == 5 && (x == 6 || x == LIFT_W - 7)) return 14;     /* 鋲 */
    return 9;
}
static void lift_sheet(u8 y0) {    /* 48x12 をページ 2 へ、y0 から 3 行(★1 フレームで 12 行書くと 2 フレーム遅れた) */
    u8 x, y;
    for (y = y0; y < y0 + 3; y++) {
        vdp_write_addr17(1, (u16)((LIFT_IMG + y) * 128u));
        for (x = 0; x < LIFT_W; x += 2) VDP_DAT = (u8)((lift_px(x, y) << 4) | lift_px((u8)(x + 1), y));
    }
}
static void spark(s16 x, s16 y) { fx_spawn(FX(x + 8 + (rnd8() & 31)), FX(y)); }
static void hangar_update(void) {
    s16 x = s5_sx(S5_COL_DOOR) + 8, y;
    if (g_boss_state == BS_ENTER) {                           /* 扉を開ける(描くのは hangar_vram。開き終えたら地図も格納庫の中に) */
        if (door_w > 32 && lift_img >= LIFT_H && !stage_busy()) {
            for (door_n = 0; door_n < 8; door_n++) g_map[S5_COL_DOOR + door_n] = 20;
            g_boss_state = BS_FIGHT;
        }
        return;
    }
    if (g_boss_state == BS_DYING) {
        if (!(boss_t & 3)) fx_spawn(FX(x + (rnd8() & 47)), FX(s5_sy(S5_DECK + 4 + (rnd8() & 31))));
        if (++boss_t == 90) door_n = 0;
        if (boss_t > 90) {                                    /* 誘爆で穴が開く */
            while (door_n < 8 && stage_redraw_col((u16)(g_w0 + S5_COL_DOOR + door_n))) { g_map[S5_COL_DOOR + door_n] = 21; door_n++; }
            if (door_n >= 8) g_boss_state = BS_DONE;
        }
        return;
    }
    /* エレベーター: Notch を載せて昇る → 射出して 1.5 秒止まる → 降りる */
    if (lift_wait) lift_wait--;
    else if (!(t & 1)) {
        if (lift_ph == 0) { lift += 2; if (lift >= LIFT_MAX) { lift = LIFT_MAX; lift_ph = 1; lift_wait = 90; enemy_spawn_notch(FX(x + 24), FX(s5_sy(lift_s() - 8))); } }
        else { lift -= 2; if (lift == 0) { lift_ph = 0; lift_wait = 40; } }
    }
    y = s5_sy(lift_s());
    g_boss_aim = (s16)(y + 6);
    if (lift_fl) lift_fl--;
    if (shot_rect(x, y - 4, LIFT_W, LIFT_H + 4)) {
        lift_fl = 4;
        if (g_boss_hp) g_boss_hp--;
        if (!(g_boss_hp & 3)) spark(x, (s16)(y + 4));          /* 火花 */
        if (!g_boss_hp) { g_boss_state = BS_DYING; boss_t = 0; score_add(2000); }
    }
    if (g_boss_hp <= 10 && !(t & 15)) spark(x, (s16)(y + 2));   /* 壊れかけ: 火花が続く */
    {   /* ランプ: 体力が減るほど速く */
        u8 m = (u8)(g_boss_hp > 20 ? 16 : g_boss_hp > 10 ? 8 : 4);
        lamp_t++;
        lift_v = (u8)(lift_fl ? 2 : (lamp_t & m) ? 1 : 0);
    }
    shot_rect(x - 8, s5_sy(S5_DECK), 64, 34);                 /* 格納庫の中は弾を止める */
}
static void hangar_draw(void) {    /* 昇る間は Notch を台に載せて見せる(上で射出する本物と同じ位置) */
    s16 x = s5_sx(S5_COL_DOOR) + 8;
    if (g_boss_state == BS_FIGHT && lift_ph == 0) spr_put((s16)(x + 16), (s16)(s5_sy(lift_s()) - 16), PAT_NOTCH, G_PAIR, 0);
}
/* ★地形のアニメ(M7 の後、C6): 扉は中央から左右へ 2 ドットずつ引き込む。開いた所は格納庫の暗がり、動く縁は白く光る。
   開き終えたら床(色 9)を引く。描いた絵は短冊 20(格納庫の中)と同じなので、地図は書き換えるだけで描き直さない */
static void door_vram(void) {
    s16 cx = s5_sx(S5_COL_DOOR) + 32, y = s5_sy(S5_DECK);
    if (door_w > 32 || gq_free() < 6) return;
    if (door_w) { ring_cmd(0, 0, cx - door_w, y, 2, 32, 1, 0x22); ring_cmd(0, 0, cx + door_w - 2, y, 2, 32, 1, 0x22); }
    if (door_w < 32) {
        ring_cmd(0, 0, cx - door_w - 2, y, 2, 32, 1, 0xFF); ring_cmd(0, 0, cx + door_w, y, 2, 32, 1, 0xFF);
    } else ring_cmd(0, 0, cx - 32, y + 32, 64, 2, 1, 0x99);
    door_w += 2;
}
/* 台の跡を消す(s0 から n 行: 甲板より上は空、下は格納庫の暗がり) */
static void lift_erase(s16 x, s16 s0, u8 n) {
    s16 k = (s16)(S5_DECK - s0);                              /* 甲板より上の行数 */
    if (k < 0) k = 0; else if (k > n) k = n;
    if (k) ring_cmd(0, 0, x, s5_sy(s0), LIFT_W, (u8)k, 1, 0x11);
    if (n > (u8)k) ring_cmd(0, 0, x, s5_sy((s16)(s0 + k)), LIFT_W, (u8)(n - (u8)k), 1, 0x22);
}
static void hangar_vram(void) {
    s16 x = s5_sx(S5_COL_DOOR) + 8, ns;
    if (g_boss_state == BS_ENTER) {
        if (lift_img < LIFT_H) { lift_sheet(lift_img); lift_img += 3; }
        door_vram(); return;
    }
    if (g_boss_state != BS_FIGHT || (lift_drawn == lift && lift_vd == lift_v) || gq_free() < 6) return;
    ns = lift_s();
    if (lift_drawn != 0xFF && lift_drawn != lift) {           /* 空いた行を消す(動きは 1 回 2 ドット) */
        s16 os = (s16)(S5_DECK + 20 - lift_drawn), d = (s16)(ns - os);
        if (d < 0) { d = -d; if (d > LIFT_H) d = LIFT_H; lift_erase(x, (s16)(os + LIFT_H - d), (u8)d); }
        else { if (d > LIFT_H) d = LIFT_H; lift_erase(x, os, (u8)d); }
    }
    if (lift_v == 2) ring_cmd(0, 0, x, s5_sy(ns), LIFT_W, LIFT_H, 1, 0xFF);   /* 当たった: 白 */
    else {
        ring_cmd(0, (u16)(512u + LIFT_IMG), x, s5_sy(ns), LIFT_W, LIFT_H, 0, 0);
        if (lift_v) ring_cmd(0, 0, (s16)(x + 18), (s16)(s5_sy(ns) + 4), 12, 4, 1, 0x22);   /* ランプが消える */
    }
    lift_drawn = lift; lift_vd = lift_v;
}

/* ======== GAUGE BLOCK ======== */
#define G_AX   184                 /* 塔の中心の画面の x */
static const s8 g_row_s[6] = { 20, 6, -8, -22, -36, -50 };    /* 段 0(下)..5(上)の上端の行(s)。隙間なく積む(艦橋の塔に見せる) */
static u8  g_order[6];             /* 段 k に置いたブロックの番号(長さは s5_glen) */
static s16 g_off[6];               /* 段のすべり(ドット。正 = 右へ) */
static u8  g_drawn;                /* 描いた段の数(登場) */
static u8  g_ph, g_a, g_b, g_redraw;
static u16 g_t;
void b5_gauge_start(void) {
    u8 k;
    for (k = 0; k < 6; k++) { g_order[k] = k; g_off[k] = 0; }
    g_drawn = 0; g_ph = 0; g_t = 0; g_redraw = 0;
    g_boss_state = BS_ENTER; g_boss_hp = 30; boss_t = 0;
    g_bands_all = 1;
}
static void gauge_row(u8 k) {      /* 段 k を描く: 空で塗ってからブロックを中央に */
    u8 n = g_order[k], L = s5_glen[n];
    s16 y = s5_sy(g_row_s[k]);
    ring_cmd(0, 0, G_AX - 56, y, 112, 14, 1, 0x11);
    ring_cmd(0, (u16)(512u + n * 14u), (s16)(G_AX - (L >> 1)), y, L, 14, 0, 0);
    if (k == 5) {                  /* いちばん上の段に測距儀とマスト */
        ring_cmd(0, 0, G_AX - 12, (s16)(y - 3), 24, 3, 1, 0x22);
        ring_cmd(0, 0, G_AX - 2, (s16)(y - 8), 4, 5, 1, 0x33);
    }
}
static void gauge_update(void) {
    u8 k;
    if (g_boss_state == BS_ENTER) { if (g_drawn >= 6) g_boss_state = BS_FIGHT; return; }
    if (g_boss_state == BS_DYING) {
        if (!(boss_t & 3)) fx_spawn(FX(G_AX - 40 + (rnd8() & 79)), FX(s5_sy(g_row_s[(rnd8() & 3) + 1]) + 6));
        if (++boss_t == 100) {
            for (k = 0; k < 6; k++) ring_cmd(0, 0, G_AX - 56, s5_sy(g_row_s[k]), 112, 14, 1, 0x11);
            ring_cmd(0, 0, G_AX - 12, (s16)(s5_sy(g_row_s[5]) - 8), 24, 8, 1, 0x11);
        }
        if (boss_t > 120) g_boss_state = BS_DONE;
        return;
    }
    g_t++;
    if (g_ph == 0) {                                          /* 止まっている: 弱点を撃てる。ブロックは撃つ */
        for (k = 0; k < 6; k++) {
            u8 L = s5_glen[g_order[k]];
            s16 y = s5_sy(g_row_s[k]);
            if (g_order[k] == 3) {                            /* 一番短い(32) */
                g_boss_aim = (s16)(y + 7);
                if (shot_rect(G_AX - 16, y - 2, 32, 18)) {
                    if (g_boss_hp) g_boss_hp--;
                    if (!g_boss_hp) { g_boss_state = BS_DYING; boss_t = 0; score_add(5000); return; }
                }
            } else shot_rect((s16)(G_AX - (L >> 1)), y, L, 14);
            if (((g_t + k * 13) & (RECAL ? 31 : 63)) == 0) enemy_shoot(FX(G_AX - (L >> 1)), FX(y + 7));
        }
        if (g_t >= (RECAL ? 70 : 150)) {                                     /* 入れ替える 2 段を選んで、右へすべらせる */
            g_a = (u8)(((u16)rnd8() * 6) >> 8); g_b = (u8)(((u16)rnd8() * 5) >> 8);
            if (g_b >= g_a) g_b++;
            g_ph = 1; g_t = 0;
        }
    } else if (g_ph == 1) {
        g_off[g_a] += RECAL ? 12 : 6; g_off[g_b] += RECAL ? 12 : 6;   /* 再校正版: 入れ替えが速い */
        if (g_off[g_a] >= 144) { k = g_order[g_a]; g_order[g_a] = g_order[g_b]; g_order[g_b] = k; g_redraw = 2; g_ph = 2; }
    } else if (g_ph == 2) {                                   /* 見えない間に描き替える(b5_vram)→ 戻す */
        if (!g_redraw && gq_free() == GQ_N) g_ph = 3;
    } else {
        g_off[g_a] -= RECAL ? 12 : 6; g_off[g_b] -= RECAL ? 12 : 6;
        if (g_off[g_a] <= 0) { g_off[g_a] = g_off[g_b] = 0; g_ph = 0; g_t = 0; }
    }
}
static void gauge_vram(void) {
    if (g_boss_state == BS_ENTER && g_drawn < 6 && gq_free() > 8 && !stage_busy()) gauge_row(g_drawn++);
    if (g_redraw && gq_free() > 8) { gauge_row(g_redraw == 2 ? g_a : g_b); g_redraw--; }
}
u8 b5_gauge_bands(s16 sc, u16 nx) {
    static const u8 sky0[3] = { 1, 0, 2 }, gap[3] = { 2, 2, 4 };
    s8 k;
    (void)sc;
    /* 段ごとの帯(上から)。一番上の段の帯は 0 行から(0 行の項目と 6 行の間を空けられないので) */
    stage_ev_pal(0, sky0);
    for (k = 5; k >= 0; k--) {
        s16 l = k == 5 ? 0 : s5_sy(g_row_s[k]);
        stage_ev_scroll(l, (u16)(nx - g_off[k]));
    }
    stage_ev_scroll(s5_sy(g_row_s[0] + 14), nx);
    stage_ev_pal(s5_sy(106), gap);
    return 1;
}

/* ======== 入口 ======== */
void b5_gauge_load(void) { gauge_ld = 0; }
void b5_skip_sheets(void) { sheet_row = 128; }
void b5_init(void) {
    u8 i;
    t = 0; spr_loaded = 0; sheet_row = 0; gauge_ld = 0xFF;
    for (i = 0; i < S5_NAA; i++) { aa_hp[i] = 2; aa_dir[i] = 3; }
    for (i = 0; i < S5_NTUR; i++) { tu_hp[i] = 12; tu_dir[i] = tu_want[i] = 8; tu_shown[i] = 0xFF; }
    for (i = 0; i < S5_NHANG; i++) { hg_hp[i] = 3; hg_dir[i] = 1; }
}

void b5_update(void) {
    if (g_boss_dmg) {                                         /* LIMIT BREAK: 中ボス・ボスに */
        u8 d = g_boss_dmg; g_boss_dmg = 0;
        if (g_boss_state == BS_FIGHT) g_boss_hp = g_boss_hp > d ? (u8)(g_boss_hp - d) : 1;
    }
    t++;
    g_boss_aim = -1;
    shots_prepare();
    if (g_sec >= SEC5_BOW && g_sec <= SEC5_HANGAR) { aa_update(); tur_update(); }
    if (g_sec == SEC5_UNDER || g_sec == SEC5_HANGAR) hang_update();
    if (g_sec == SEC5_HANGAR && g_boss_state != BS_NONE && g_boss_state != BS_DONE) hangar_update();
    if (g_sec == SEC5_BOSS && g_boss_state != BS_NONE && g_boss_state != BS_DONE) gauge_update();
    if (g_sec == SEC5_ENGINE && gauge_ld == 0xFF) b5_gauge_load();
}

void b5_draw(void) {
    if (g_sec >= SEC5_BOW && g_sec <= SEC5_HANGAR) aa_draw();
    if (g_sec == SEC5_UNDER || g_sec == SEC5_HANGAR) hang_draw();
    if (g_sec == SEC5_HANGAR) hangar_draw();
}

void b5_vram(void) {
    u8 n;
    if (!spr_loaded && (u16)(sys_timer() - g_frame_t0) < 100u * 16u) {   /* この面のスプライト(機銃 8 + 吊り砲台 3) */
        rom_to_vram(ASSET_S5_SPR_BANK, ASSET_S5_SPR_OFF, 1, (u16)(0xF800u + PAT_AA * 32u), 256);
        rom_to_vram(ASSET_S5_SPR_BANK, (u16)(ASSET_S5_SPR_OFF + 256u), 1, (u16)(0xF800u + PAT_HANG * 32u), 96);
        spr_loaded = 1;
    }
    /* ページ 2 へ: 主砲塔 16 向き(0..63 行)。1 フレーム 4 行、重いフレームは次へ */
    if (sheet_row < 64 && (u16)(sys_timer() - g_frame_t0) < 150u * 16u)
        for (n = 0; n < 2; n++, sheet_row++)
            rom_to_vram(ASSET_TURRET_BANK, (u16)(ASSET_TURRET_OFF + sheet_row * 128u), 1, (u16)(sheet_row * 128u), 128);
    if (gauge_ld < 84 && (u16)(sys_timer() - g_frame_t0) < 150u * 16u)   /* GAUGE BLOCK(主砲塔の場所へ上書き) */
        for (n = 0; n < 4 && gauge_ld < 84; n++, gauge_ld++)
            rom_to_vram(ASSET_GAUGE_BANK, (u16)(ASSET_GAUGE_OFF + gauge_ld * 56u), 1, (u16)(gauge_ld * 128u), 56);
    if (g_sec >= SEC5_BOW && g_sec <= SEC5_HANGAR) tur_vram();
    if (g_sec == SEC5_HANGAR) hangar_vram();
    if (g_sec == SEC5_BOSS) gauge_vram();
    gq_pump(0);
}
