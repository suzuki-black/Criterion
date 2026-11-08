/* s5.c — 5 面「規格戦艦ノルム」のオーバレイ: 区間・カメラ・帯・当たり・接近・撃沈(指示書 5.5)。
   宇宙 → 雲海の上へ降りる → 接近(世界を止め、雲だけ帯で流す。遠くの戦艦が 1 ドットずつ近づいて、最後の段で本編の
   艦首そのものになり、地図を差し替えて本物の列へすり替える。第 4 バンクの farz.c)→ 艦首(機銃)→ 艦体上面(主砲塔 4 基)→ 中ボス HANGAR DECK(誘爆で艦体に穴)→ 穴から艦底へ(上半分が
   艦底、下半分が雲海)→ 装甲の破れ目から機関部(天井と床のプレスが R#23 の帯で上下)→ 出口の縦穴から艦橋へ →
   ボス GAUGE BLOCK → 撃沈(前後の帯が離れながら沈む)→ 宇宙へ。
   ・カメラ: 甲板の上は s = -60(上 60 行は高い空)、艦底と機関部は s = 0。
   ・当たり: 区間ではなく自機のいる側で決める(艦体の上 / 下 / 室内)。穴と縦穴の列はどちらにも行ける。
   ・色 6 は空の色(短冊の 0..3 行 = ブロック 23 が空に溶ける)。艦の暗い色は 2。
   ★読み込まれるのは 5 面の空中戦が始まってから(4 面のオーバレイが頼む)。 */
#include "s5.h"
#include "ovl.h"
#include "vdp.h"
#include "rom.h"
#include "assets.h"
#include "s5_data.h"
#include "ghost.h"
#include "spr.h"
#include "sprite_data.h"

#define AIR_LEN  1200              /* 空中戦の残り(ドット。★4〜6 秒は短すぎた。約 20 秒に。M7 の後) */
#define SINK_LEN 256
#define S_TOP    (-192)

static u16 sec_wx, sec_t;
static u8  side;                   /* 自機のいる側: 0 = 艦体の上、1 = 下、2 = 室内 */
static s16 cam_to;                 /* カメラの行き先(s × 16) */
static u16 cq;                     /* 接近の間の雲の流れ(1/4 ドット)。艦首の区間からは雲の帯のずれ(すり替えで雲が跳ばないように) */
static u8  far_on;                 /* 接近: 0 = 止める位置へ流す、1 = 近づく、2 = 見せ終えた、3 = 艦の続きを描いている */
static u8  fz_d;                   /* 艦首を左へずらすドット(空の帯の横スクロール) */
static u8  fz_buf[256];            /* farz.c が ROM の塊を読む場所(ページ 3) */
static u8  vortex;
static u8  press_on;
s8 s5_press_c, s5_press_f;

static const u8 sky0[3] = { 1, 0, 2 }, sky1[3] = { 2, 1, 3 }, sky2[3] = { 3, 2, 4 }, sky3[3] = { 4, 3, 5 };
static const u8 inside[3] = { 1, 1, 2 }, gap[3] = { 2, 2, 4 };
static const u8 *const skies[4] = { sky0, sky1, sky2, sky3 };

s16 s5_sx(u16 col) { return (s16)((u16)((g_w0 + col) << 3) - g_wx); }
s16 s5_sy(s16 s) { return (s16)(s - g_scam); }
u16 s5_col_at(s16 sx) { return (u16)(((u16)(g_wx + (u16)sx) >> 3) - g_w0); }
static u16 col_of_screen(void) { return s5_col_at(0); }

static void enter(u8 sec) {
    g_sec = sec; sec_wx = g_wx; sec_t = 0;
    switch (sec) {
    case SEC5_AIR: enemy_waves(WAVES_SKY); break;
    case SEC5_APPROACH: enemy_waves(WAVES_AIR); break;
    case SEC5_BOW: case SEC5_DECK: enemy_waves(WAVES_CITY); break;
    default: enemy_waves(WAVES_NONE); break;
    }
    if (sec == SEC5_HANGAR) b5_hangar_start();
    if (sec == SEC5_BOSS) b5_gauge_start();
}

static void palette(void) {
    vdp_set_pal(6, 1, 0, 2);                                  /* = 空(ブロック 23 を空に溶かす) */
    vdp_set_pal(2, 1, 1, 2);                                  /* 艦の暗い色 */
    vdp_set_pal(3, 3, 3, 4); vdp_set_pal(4, 5, 5, 6); vdp_set_pal(5, 6, 6, 7); vdp_set_pal(7, 7, 7, 7);   /* 雲海 */
}

void ovl_init(void) {
    g_ter.lo = s5_bot; g_ter.hi = s5_top; g_ter.lo_add = 0; g_ter.ncol = S5_COLS;   /* DROP の爆雷が這う床・天井 */
    stage_reset_ground();
    g_w0 = (u16)((g_wx + AIR_LEN) >> 3);
    rom_to_ram(ASSET_S5_MAP_BANK, ASSET_S5_MAP_OFF, g_map, S5_COLS);
    g_maplen = S5_COLS;
    stage_load_strips(ASSET_S5_STRIPS_BANK, ASSET_S5_STRIPS_OFF);
    g_boss_state = BS_NONE; g_grav = GRAV_NONE; g_col_rev = 0; g_sky_t = 16;
    side = 0; far_on = 0; fz_d = 0; press_on = 0; cq = 0; vortex = 0;
    s5_press_c = s5_press_f = 0; cam_to = S_TOP * 16;
    b5_init();
    if (g_calib) {                                            /* 7 面の試験 05: 白い空間から艦橋の甲板へ降りて GAUGE BLOCK */
        u16 c;
        g_w0 = (u16)((g_wx >> 3) - (S5_COL_BOSS - 64));
        for (c = S5_COL_BOSS - 56; c < S5_COL_BOSS; c++) g_map[c] = 11;   /* 手前は平らな甲板にする */
        palette();
        b5_skip_sheets();
        calib_begin(5);
        g_scroll_speed = 4; cam_to = S_TOP * 16;              /* 短冊を読み終えるまで白い空間で待つ */
        enter(SEC5_BOW);
        return;
    }
    enter(SEC5_AIR);
}

/* ---- 当たり(自機の上下の限り) ---- */
static void terrain(void) {
    u16 c0 = s5_col_at(PX(g_pl.x)), c;
    s16 lo = 0, hi = PLAY_H - 16, py = PX(g_pl.y) + 8 + g_scam;   /* 自機の中心の s */
    u8 k;
    /* いる側: 室内の列にいれば室内、艦体の列では今の側のまま(穴の列でだけ上下が替わる) */
    k = c0 < S5_COLS ? g_map[c0] : 0;
    {
        /* ★船底の下に自機の通れる余裕(20 ドット)があるときだけ「下を通る側」にする。カメラの高さによっては船底の下が
           12 ドットしか無く、低く飛んでいた自機が船底の下へ押し込まれて閉じ込められ、上へ入れても押し戻されるので
           下へ引かれる重力のように感じた(指摘「戦艦面の謎の重力」) */
        u8 under = (s16)(PLAY_H - 16) - (s16)(S5_HULL_B - g_scam) >= 20;
        if (s5_kind[k] == 2) side = 2;
        else if (s5_kind[k] == 0) side = under && py > S5_HULL_B - 30 ? 1 : 0;
        else if (side == 2) side = 0;                         /* 室内から出た: 甲板の側へ(★この先はボスのためにカメラが甲板の高さへ
                                                                 上がり、船底の下の通り道は 12 ドットまで狭まる。低く出ると下の側にされて
                                                                 雲の上へ押し付けられ、甲板へ戻れなかった。遊んでの指摘) */
    }
    for (c = c0; c <= (u16)(c0 + 2); c++) {
        s16 t, b;
        if (c >= S5_COLS) continue;
        k = g_map[c];
        t = s5_top[k]; b = s5_bot[k];
        if (s5_kind[k] == 1) {
            if (side == 1) { if (b - g_scam > lo) lo = b - g_scam; }
            else if (t - 16 - g_scam < hi) hi = t - 16 - g_scam;
        } else if (s5_kind[k] == 2) {
            if (k != 27) { t += s5_press_c; b -= s5_press_f; }   /* 出口の縦穴(27)だけはプレスが無い */
            if (t - g_scam > lo) lo = t - g_scam;
            if (b - 16 - g_scam < hi) hi = b - 16 - g_scam;
        }
    }
    if (hi < lo) { if (side == 2) player_hit(); hi = lo; }    /* プレスに挟まれた */
    g_pl_ymin = lo < 0 ? 0 : lo;
    g_pl_ymax = hi > PLAY_H - 16 ? PLAY_H - 16 : hi;
}

void ovl_update(void) {
    u16 c;
    sec_t++;
    switch (g_sec) {
    case SEC5_AIR:                                            /* 宇宙(短冊を読む間) */
        if (g_scroll_speed != 4 && !(sec_t & 3)) g_scroll_speed += g_scroll_speed < 4 ? 1 : -1;
        if ((u16)(g_wx - sec_wx) >= AIR_LEN) { palette(); enter(SEC5_SINK); }
        break;
    case SEC5_SINK: {                                         /* 雲海の上へ降りる */
        u16 d = (u16)(g_wx - sec_wx);
        if (d < SINK_LEN) g_scam_q = (s16)(S_TOP * 16 + (s16)(d * 12u));
        else if (!stage_busy()) { g_scam_q = 0; enter(SEC5_APPROACH); }
        break;
    }
    case SEC5_APPROACH:                                       /* 世界は止め、雲だけ流す。戦艦が近づいて艦首そのものになる */
        if (far_on < 2) cq += 6;
        if (far_on == 0) {
            /* 画面の x = S5_FZ_XB が、地図で 4 列おきの列の左端に来たら止める(すり替えで地図の対応を替えても、手前の空と雲の短冊が
               同じ並びのままになる。雲海の短冊は 4 本の周期) */
            if (!(((u16)(g_wx + S5_FZ_XB) - (g_w0 << 3)) & 31)) {
                g_scroll_speed = 0;
                if (sec_t > 60) { far_on = 1; fz_start(fz_buf); }
            } else g_scroll_speed = 4;                       /* 1 ドットずつ(飛ばさずに止める位置に来る) */
        } else if (far_on == 2) {
            /* すり替え: 画面の艦首の絵は本編の短冊と同じなので、地図の対応を替えて本物の艦首の列にする。見えない半分(艦の続き)は描き直す。
               機銃はスプライトになるので、絵に描いた機銃を背景へ戻す */
            g_w0 = (u16)(((g_wx + S5_FZ_XB) >> 3) - S5_COL_BOW);
            stage_cols_redo(32);
            stage_restore((s16)(s5_sx(s5_aa[0]) - 4), S5_DECK - 20, 16, 16);
            cq += (bg_layer_x(2) >> 1) << 2;                  /* 雲の帯の位置を艦首の区間の式(nx - nx / 2)で続ける */
            far_on = 3;
        } else if (far_on == 3 && !stage_busy()) {            /* 艦の続きを描き終えたら流し始める */
            enter(SEC5_BOW); cam_to = S_DECKVIEW * 16;
        }
        break;
    case SEC5_BOW:
        if (g_calib) {
            if (sec_t == 120) { cam_to = S_DECKVIEW * 16; b5_gauge_load(); }   /* 短冊が揃ってから降りる */
            /* ★降り切ったら画面の上まで面の空にする(このカメラは s < 0 が見えるので、測定空間の白が甲板の上に残り続けた。遊んでの指摘) */
            if (sec_t > 120 && g_scam_q == cam_to) g_bands_all = 1;
            if (col_of_screen() + 8 >= S5_COL_BOSS + 8) { g_scroll_speed = 0; enter(SEC5_BOSS); }
            break;
        }
        if (g_scroll_speed < 4 && !(sec_t & 7)) g_scroll_speed++;   /* 止まっている艦首から流し始める */
        if (sec_t < 96) cq += (u8)(3 - (sec_t >> 5));         /* 雲の流れを接近の速さから落とす */
        if (col_of_screen() + 16 >= S5_COL_DECK) enter(SEC5_DECK);
        break;
    case SEC5_DECK:
        if (s5_col_at(160) >= S5_COL_DOOR + 4) { g_scroll_speed = 0; enter(SEC5_HANGAR); }   /* 扉が画面の右寄りで止まる */
        break;
    case SEC5_HANGAR:
        /* ★誘爆が始まったら艦底の眺めへ下ろし始める(穴が開く頃に下を通る余裕ができる)。前は穴を抜けるか 5 秒たつまで甲板の
           眺めのままで、船底の下に通る余裕が無いので「上の側」にされ、穴から下へ行こうとすると押し戻された(遊んでの指摘
           「上にいていいのか下にいていいのかわからない」) */
        if (g_boss_state == BS_DYING) cam_to = 0;
        if (sec_t == 5400) g_boss_state = BS_DONE;
        if (g_boss_state == BS_DONE) { g_boss_state = BS_NONE; g_scroll_speed = 4; enter(SEC5_UNDER); stage_banner_c("DIVE UNDER THE HULL", 13); }
        break;
    case SEC5_UNDER:
        cam_to = 0;                                           /* 艦底の眺め */
        if (col_of_screen() >= S5_COL_ENGINE + 1) { g_scroll_speed = 3; enter(SEC5_ENGINE); }
        break;
    case SEC5_ENGINE:
        /* 画面がすべて室内の間だけプレスを動かす(帯は画面の全幅なので、艦体や雲の列が見えている間は使えない) */
        c = col_of_screen();
        press_on = c > S5_COL_ENGINE && c + 33 < S5_COL_ENGINE + 88;
        if (press_on) {
            s5_press_c = (s8)(2 + ((u8)(g_sin64[(u8)(sec_t >> 1) & 63] + 128) >> 4));
            s5_press_f = (s8)(2 + ((u8)(g_sin64[(u8)((sec_t >> 1) + 32) & 63] + 128) >> 4));
        } else {
            if (s5_press_c) s5_press_c--;
            if (s5_press_f) s5_press_f--;
        }
        if (c + 16 >= S5_COL_BOSS) { cam_to = S_DECKVIEW * 16; g_scroll_speed = 4; }
        if (c + 8 >= S5_COL_BOSS + 8) { g_scroll_speed = 0; enter(SEC5_BOSS); }
        break;
    case SEC5_BOSS:
        if (sec_t == 5400) g_boss_state = BS_DONE;
        if (g_boss_state == BS_DONE) {
            g_boss_state = BS_NONE; g_bands_all = 0;
            if (g_calib) { g_scroll_speed = 4; enter(SEC5_OUT); } else enter(SEC5_WRECK);
        }
        break;
    case SEC5_WRECK:                                          /* 撃沈: 2 秒爆発 → カメラが上がり、艦が画面の下へ沈む。
                                                                 ★前は前後の帯を R#23 で別々に沈めていたが、R#23 はスプライトもずらすので
                                                                 自機が 2 機に見え、帯の空も割れた(遊んでの指摘「画面が壊れる」) */
        if (sec_t < 120) {
            if (!(sec_t & 3)) fx_spawn(FX(100 + (rnd8() & 63)), FX(s5_sy(40 + (rnd8() & 31))));
            if (sec_t == 100 && gq_free() >= 8) {             /* 艦体の中央に裂け目(前後の帯が離れると、上と下で逆へずれて見える) */
                ring_cmd(0, 0, 120, s5_sy(S5_DECK - 6), 16, 34, 1, 0x22);
                ring_cmd(0, 0, 126, s5_sy(S5_DECK + 28), 16, 36, 1, 0x22);
                ring_cmd(0, 0, 122, s5_sy(S5_DECK + 10), 4, 30, 1, 0xDD);
            }
        } else {
            cam_to = S_TOP * 16;
            if (!(sec_t & 3)) {                               /* 雲海の渦(パレットを回す) */
                u8 p = vortex;
                if (++vortex >= 3) vortex = 0;
                vdp_set_pal(4, (u8)(4 + p), (u8)(4 + p), (u8)(5 + p));   /* 雲のきめ(4, 5)だけ回す(3 は雲の地で、回すと全面が光った) */
                vdp_set_pal(5, (u8)(6 - p), (u8)(6 - p), 7);
            }
            if (sec_t > 300) { enter(SEC5_OUT); g_scroll_speed = 4; }
        }
        g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16;
        break;
    case SEC5_OUT:                                            /* 宇宙へ上がる(沈んだ帯のまま) */
        if (g_calib) { cam_to = S_TOP * 16; if (calib_out(sec_t)) return; break; }
        cam_to = S_TOP * 16;
        if (sec_t == 1) stage_sky_redraw();                   /* ★空のブロックを描き直させる(甲板の艦橋を描いたリングの行が 6 面では空の行に
                                                                 当たり、空の印が古いまま残って、6 面の空に艦橋が 1 周ごとに見えた。遊んでの指摘) */
        if (sec_t == 240) {
            palette();
            stage_banner("STAGE 6 / GNOMON CITADEL");
            ovl_request(OVL_S6);
            g_pl_dy = 0;
            g_sec = SEC5_OUT + 1;                              /* = 6 面の空中戦(SEC6_AIR) */
            return;
        }
        break;
    default:
        break;
    }
    /* カメラ(接近と艦底・機関部は 0、甲板の上は -60) */
    if (g_sec >= SEC5_BOW) {
        if (g_scam_q < cam_to) { g_scam_q += 16; if (g_scam_q > cam_to) g_scam_q = cam_to; }
        else if (g_scam_q > cam_to) { g_scam_q -= 16; if (g_scam_q < cam_to) g_scam_q = cam_to; }
    }
    g_pl_dy = 0;
    if (g_sec == SEC5_WRECK || g_sec == SEC5_OUT) return;
    if (g_sec < SEC5_APPROACH) { g_pl_ymin = 0; g_pl_ymax = PLAY_H - 16; }
    else terrain();
}

void ovl_logic(void) { b5_update(); }
void ovl_draw(void) {
    if (g_sec == SEC5_OUT && !g_calib && sec_t >= 30) {      /* 雲海の向こうに日時計の要塞が見えてくる(6 面の予告) */
        s16 x = (s16)(250 - (sec_t >> 2)), y = (s16)(118 - (g_scam_q >> 4) + S_TOP);
        fort_draw(x, y);                                      /* 第 4 バンク */
    }
    b5_draw();
}
void ovl_vram(void) {
    if (g_sec == SEC5_APPROACH && far_on == 1) {
        u16 r = fz_vram();
        if (r & 0x8000u) far_on = 2; else fz_d = (u8)r;
    }
    b5_vram();
}

void ovl_bands(s16 sc, u16 nx) {
    if (g_bands_all) {
        if (g_sec == SEC5_BOSS) { b5_gauge_bands(sc, nx); return; }
        if (g_sec != SEC5_BOW) return;
    }
    if (g_sec < SEC5_SINK) return;
    /* 空のグラデーション(色 1)と雲海。接近の間は雲の帯だけ流れ、空の帯は艦首の動きの分だけずらす */
    if (g_sec == SEC5_APPROACH) stage_ev_ps((s16)(0 - sc), sky0, (u16)(nx + fz_d));
    else stage_ev_pal(g_bands_all ? 0 : (s16)(0 - sc), sky0);
    if (g_sec <= SEC5_BOW) {                                  /* 艦首の区間の頭で 1 段ずつ消す(★閃光の間に消していたのをやめたので) */
        s8 lv = g_sec != SEC5_BOW ? 0 : g_calib || sec_t >= 96 ? 3 : (s8)(sec_t >> 5), k;
        for (k = 1; k <= 3; k++) if (k > lv) stage_ev_pal((s16)(k * 28 - sc), skies[k - lv]);
    }
    if (g_sec == SEC5_ENGINE) stage_ev_pal((s16)(0 - sc), inside);
    /* 天井のプレス: 24 行から下を e だけ下げて見せる(★帯の項目は行の順に出す) */
    if (press_on) {
        u8 base = (u8)(g_vscroll + g_roll);
        stage_ev_vscroll((s16)(24 - sc), (u8)(base - s5_press_c));
        stage_ev_vscroll((s16)(S5_CEIL + s5_press_c - sc), base);
    }
    stage_ev_pal((s16)(106 - sc), g_sec == SEC5_ENGINE ? inside : gap);   /* 艦底と雲海の間(2 帯) */
    if (g_sec == SEC5_APPROACH) stage_ev_scroll((s16)(108 - sc), (u16)(nx + (cq >> 2)));
    else if (g_sec >= SEC5_BOW && g_sec <= SEC5_UNDER)        /* ★雲海は戦艦の半分の速さ(同じ速さでは奥行きが出ない。M7 の後) */
        stage_ev_scroll((s16)(108 - sc), (u16)(nx - (nx >> 1) + (cq >> 2)));
    if (press_on) {                                           /* 床のプレス: 上げて見せる */
        u8 base = (u8)(g_vscroll + g_roll);
        stage_ev_vscroll((s16)(S5_FLOOR - s5_press_f - sc), (u8)(base + s5_press_f));
        stage_ev_vscroll((s16)(164 - sc), base);
    }
}
