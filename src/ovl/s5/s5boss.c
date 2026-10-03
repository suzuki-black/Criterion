/* s5boss.c — 5 面の砲台、中ボス HANGAR DECK、ボス GAUGE BLOCK(指示書 5.5)。
   ・機銃(艦首・甲板): スプライト。自機の方へ 8 向き(上の半円、プリレンダ)で旋回して撃つ。
   ・主砲塔(甲板 4 基): 台は短冊(BG)、上の 32x32 は 16 向き(左 → 真上 → 右)をプリレンダしてページ 2 に置き、
     向きが変わったときだけ表示リングへ HMMM で写す(GHOST)。壊すと砲身が吹き飛ぶ(空で塗る)。
   ・吊り砲台(艦底): スプライト 3 向き。艦底からは爆雷(落ちる弾)。
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
    dx >>= 2; dy >>= 2;                                       /* 掛け算の桁を抑える */
    for (i = 0; i < n; i++) {
        s16 d = (s16)(dx * ux[i] + dy * uy[i]);
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
        if (!((t + i) & 7)) aa_dir[i] = best_dir(plx() - x, ply() - y, aa_ux, aa_uy, 8);
        if (shot_rect(x - 7, y - 7, 14, 14) && --aa_hp[i] == 0) { fx_spawn(FX(x), FX(y)); score_add(30); continue; }
        if (((t + i * 29) & 127) == 0 && x > 16 && x < 240)
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
        if (shot_rect(x + 4, cy - 14, 24, 22) && --tu_hp[i] == 0) {
            fx_spawn(FX(cx), FX(cy)); fx_spawn(FX(cx - 10), FX(cy - 8)); fx_spawn(FX(cx + 10), FX(cy - 12));
            score_add(500);
            tu_shown[i] = 0xFE;                               /* 砲身を吹き飛ばす(空で塗る) */
            continue;
        }
        if (((t + i * 37) & 63) == 0 && x > 0 && x < 224)
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

/* ======== 吊り砲台と爆雷(艦底) ======== */
static u8 hg_hp[S5_NHANG], hg_dir[S5_NHANG];
static void hang_update(void) {
    u8 i;
    for (i = 0; i < S5_NHANG; i++) {
        s16 x = s5_sx(s5_hang[i]) + 4, y = s5_sy(S5_HULL_B + 4) + 6;
        s16 dx;
        if (!hg_hp[i] || x < -8 || x > 264) continue;
        dx = plx() - x;
        hg_dir[i] = dx < -24 ? 0 : dx > 24 ? 2 : 1;
        if (shot_rect(x - 7, y - 7, 14, 14) && --hg_hp[i] == 0) { fx_spawn(FX(x), FX(y)); score_add(50); continue; }
        if (((t + i * 23) & 63) == 0 && x > 16 && x < 240)
            enemy_bullet(FX(x), FX(y + 4), (s16)((hg_dir[i] - 1) * 24), 36);
    }
    if (g_sec == SEC5_UNDER && !(t & 31)) {                   /* 爆雷: 艦底からゆっくり落ちる */
        s16 x = (s16)(32 + (rnd8() & 127) + (rnd8() & 63));
        enemy_bullet(FX(x), FX(s5_sy(S5_HULL_B + 2)), -8, 20);
    }
}
static void hang_draw(void) {
    u8 i;
    for (i = 0; i < S5_NHANG; i++) {
        s16 x = s5_sx(s5_hang[i]) + 4;
        if (!hg_hp[i] || x < -8 || x > 264) continue;
        spr_put(x - 8, s5_sy(S5_HULL_B + 4) - 2, (u8)(PAT_HANG + hg_dir[i]), G_ENEMY, 0);
    }
}

/* ======== HANGAR DECK ======== */
#define LIFT_MAX 52                /* 板は格納庫の底(s 68)から甲板の上(s 16)まで昇る(★甲板より下は自機の弾が届かない) */
static u8 door_n;                  /* 書き換えた扉の列 */
static u8 lift, lift_drawn, lift_ph, lift_wait;
static u8 boss_t;
static s16 lift_s(void) { return (s16)(S5_DECK + 28 - lift); }
void b5_hangar_start(void) { door_n = 0; lift = 0; lift_drawn = 0xFF; lift_ph = 0; lift_wait = 60; g_boss_state = BS_ENTER; g_boss_hp = 30; boss_t = 0; }
static void hangar_update(void) {
    s16 x = s5_sx(S5_COL_DOOR) + 8, y;
    if (g_boss_state == BS_ENTER) {                           /* 扉を開ける(地図を書き換えて列を描き直す) */
        while (door_n < 8 && stage_redraw_col((u16)(g_w0 + S5_COL_DOOR + door_n))) { g_map[S5_COL_DOOR + door_n] = 20; door_n++; }
        if (door_n >= 8 && !stage_busy()) g_boss_state = BS_FIGHT;
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
    /* エレベーター: 昇る → Notch を射出 → 降りる */
    if (lift_wait) lift_wait--;
    else if (!(t & 1)) {
        if (lift_ph == 0) { lift += 2; if (lift >= LIFT_MAX) { lift = LIFT_MAX; lift_ph = 1; lift_wait = 30; enemy_spawn_notch(FX(x + 24), FX(s5_sy(lift_s() - 8))); } }
        else { lift -= 2; if (lift == 0) { lift_ph = 0; lift_wait = 40; } }
    }
    y = s5_sy(lift_s());
    g_boss_aim = (s16)(y + 2);
    if (shot_rect(x, y - 6, 48, 14)) {
        if (g_boss_hp) g_boss_hp--;
        if (!g_boss_hp) { g_boss_state = BS_DYING; boss_t = 0; score_add(2000); }
    }
    shot_rect(x - 8, s5_sy(S5_DECK), 64, 34);                 /* 格納庫の中は弾を止める */
}
static void hangar_vram(void) {
    s16 x = s5_sx(S5_COL_DOOR) + 8;
    if (g_boss_state != BS_FIGHT || lift_drawn == lift || gq_free() < 6) return;
    if (lift_drawn != 0xFF) {                                 /* 前の位置を消す: 甲板より上は空、下は格納庫の暗がり */
        s16 os = (s16)(S5_DECK + 28 - lift_drawn);
        if (os + 4 <= S5_DECK) ring_cmd(0, 0, x, s5_sy(os), 48, 4, 1, 0x11);
        else if (os >= S5_DECK) ring_cmd(0, 0, x, s5_sy(os), 48, 4, 1, 0x22);
        else { ring_cmd(0, 0, x, s5_sy(os), 48, (u8)(S5_DECK - os), 1, 0x11); ring_cmd(0, 0, x, s5_sy(S5_DECK), 48, (u8)(os + 4 - S5_DECK), 1, 0x22); }
    }
    ring_cmd(0, 0, x, s5_sy(lift_s()), 48, 4, 1, 0x99);
    lift_drawn = lift;
}

/* ======== GAUGE BLOCK ======== */
#define G_AX   184                 /* 塔の中心の画面の x */
static const s8 g_row_s[6] = { 20, 6, -14, -28, -42, -56 };   /* 段 0(下)..5(上)の上端の行(s)。0..5 は避ける */
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
}
static void gauge_update(void) {
    u8 k;
    if (g_boss_state == BS_ENTER) { if (g_drawn >= 6) g_boss_state = BS_FIGHT; return; }
    if (g_boss_state == BS_DYING) {
        if (!(boss_t & 3)) fx_spawn(FX(G_AX - 40 + (rnd8() & 79)), FX(s5_sy(g_row_s[(rnd8() & 3) + 1]) + 6));
        if (++boss_t == 100) for (k = 0; k < 6; k++) ring_cmd(0, 0, G_AX - 56, s5_sy(g_row_s[k]), 112, 14, 1, 0x11);
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
            if (((g_t + k * 13) & 63) == 0) enemy_shoot(FX(G_AX - (L >> 1)), FX(y + 7));
        }
        if (g_t >= 150) {                                     /* 入れ替える 2 段を選んで、右へすべらせる */
            g_a = (u8)(((u16)rnd8() * 6) >> 8); g_b = (u8)(((u16)rnd8() * 5) >> 8);
            if (g_b >= g_a) g_b++;
            g_ph = 1; g_t = 0;
        }
    } else if (g_ph == 1) {
        g_off[g_a] += 6; g_off[g_b] += 6;
        if (g_off[g_a] >= 144) { k = g_order[g_a]; g_order[g_a] = g_order[g_b]; g_order[g_b] = k; g_redraw = 2; g_ph = 2; }
    } else if (g_ph == 2) {                                   /* 見えない間に描き替える(b5_vram)→ 戻す */
        if (!g_redraw && gq_free() == GQ_N) g_ph = 3;
    } else {
        g_off[g_a] -= 6; g_off[g_b] -= 6;
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
    if (g_sec == SEC5_ENGINE && gauge_ld == 0xFF) gauge_ld = 0;   /* GAUGE BLOCK のブロックをページ 2 へ読み始める */
}

void b5_draw(void) {
    if (g_sec >= SEC5_BOW && g_sec <= SEC5_HANGAR) aa_draw();
    if (g_sec == SEC5_UNDER || g_sec == SEC5_HANGAR) hang_draw();
}

void b5_vram(void) {
    u8 n;
    if (!spr_loaded) {                                        /* この面のスプライト(機銃 8 + 吊り砲台 3) */
        rom_to_vram(ASSET_S5_SPR_BANK, ASSET_S5_SPR_OFF, 1, (u16)(0xF800u + PAT_AA * 32u), 256);
        rom_to_vram(ASSET_S5_SPR_BANK, (u16)(ASSET_S5_SPR_OFF + 256u), 1, (u16)(0xF800u + PAT_HANG * 32u), 96);
        spr_loaded = 1;
    }
    /* ページ 2 へ: 主砲塔 16 向き(0..63 行)と遠くの戦艦(64..127 行)。1 フレーム 4 行、重いフレームは次へ */
    if (sheet_row < 128 && (u16)(sys_timer() - g_frame_t0) < 150u * 16u)
        for (n = 0; n < 4; n++, sheet_row++) {
            if (sheet_row < 64) rom_to_vram(ASSET_TURRET_BANK, (u16)(ASSET_TURRET_OFF + sheet_row * 128u), 1, (u16)(sheet_row * 128u), 128);
            else rom_to_vram(ASSET_FAR_BANK, (u16)(ASSET_FAR_OFF + (sheet_row - 64) * 128u), 1, (u16)(sheet_row * 128u), 128);
        }
    if (gauge_ld < 84 && (u16)(sys_timer() - g_frame_t0) < 150u * 16u)   /* GAUGE BLOCK(主砲塔の場所へ上書き) */
        for (n = 0; n < 4 && gauge_ld < 84; n++, gauge_ld++)
            rom_to_vram(ASSET_GAUGE_BANK, (u16)(ASSET_GAUGE_OFF + gauge_ld * 56u), 1, (u16)(gauge_ld * 128u), 56);
    if (g_sec >= SEC5_BOW && g_sec <= SEC5_HANGAR) tur_vram();
    if (g_sec == SEC5_HANGAR) hangar_vram();
    if (g_sec == SEC5_BOSS) gauge_vram();
    gq_pump(0);
}
