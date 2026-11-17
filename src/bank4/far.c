/* far.c — 遠景の帯の横位置(多重スクロール。stage.c の g_far)。第 4 バンク(2026-10-28 に第 2 バンクから移した): 常駐と 1 面のオーバレイに空きが無いので
   面で共通の計算をここに置く。面は ovl_bands で far_step(nx | 指示) を呼び、返った位置が nx とずれていれば帯の境に項目を出す。
   ・FAR_FREE: bg の動きの半分だけ進む(端数は持ち越す)
   ・FAR_LOCK: bg と一緒に動きつつ、ずれ(512 で回る)を 1/8 ずつ 0 へ戻す(真ん中に壁やボスの BG がある所の手前で)
   ・FAR_RESET: 置き直す(面の入口。ずれ 0 から)
   ・FAR_CITY: 1 面。流す・戻すをここで決める(下の far_city)
   ★本体の変数の領域に空きが無いので、指示は引数の上位 2 ビット(bg の位置は 14 ビット)、状態はここ(第 2 バンクの変数)に置く */
#include "game.h"
#include "vdp.h"

static u16 fx, fprev;
static s16 facc;
static u16 far_core(u16 a) {
    u16 nx = a & 0x3FFF;
    s16 d = (s16)((nx - fprev) & 0x3FFF);                    /* bg の位置は 16384 で回る */
    if (a & FAR_RESET) { fx = nx; d = 0; facc = 0; }
    else if (d >= 0x2000) d -= 0x4000;
    fprev = nx;
    if (a & FAR_FREE) {
        s16 h;
        facc += d; h = facc >> 1; fx += (u16)h; facc -= (s16)(h * 2);
    } else {
        s16 e;
        fx += (u16)d;
        e = (s16)((nx - fx) & 511u);
        if (e >= 256) e -= 512;
        fx += (u16)(e >= 0 ? (e + 7) >> 3 : -((-e + 7) >> 3));
        facc = 0;
    }
    return fx;
}

/* ---- 1 面(都市): 雲の底の下の遠景のビル群(gen_city.py。s = 20..67 はどの短冊も空で、遠景の短冊だけが描く)。
   奥(14..43 行)は 1/4、手前(44..69 行)は 1/2 の速さ。帯の横位置は手前 = 戻り値、奥 = f->xb(オーバレイの変数。ページ 3 なので書ける)。
   奥のずれは手前の 1.5 倍(e + e/2)。e が 512 で回って符号が替わると奥は 768 ≡ 256 跳ぶが、遠景の周期 128 の倍数なので見えない ---- */
#define CITY_FAR_ONLY
#include "city_data.h"
#define FAR_AHEAD 72               /* リングは画面の左から 64 列(画面 + 32 列先)。帯をずらすとその全部が見えるので、広場(遠景の無い短冊)の
                                      列が入る 8 列手前でずれを 0 にし終え、広場の列がリングから出るまで流さない */
#define FAR_HIDE  56               /* 広場の 56 列手前から遠景を空の色へ溶かす(画面の右に広場が出るのは 32 列手前。そこで縦に切れて見えた) */
extern s16 g_blk_tag[];            /* stage.c(ページ 3 の変数) */
extern s16 g_pick_at;

/* 遠景の色(2 = 奥、3 = 手前、4 = 奥の胴体の手前の帯に入る行、5 = 窓の灯)の段: 0 = 空と同じ(2 は 14..43 行の空、ほかは 44..69 行の空)… 3 = 見える */
static const u8 far_pal[4][4][3] = {
    { { 3, 1, 3 }, { 4, 1, 3 }, { 4, 1, 3 }, { 4, 1, 3 } },
    { { 3, 1, 3 }, { 3, 1, 3 }, { 3, 1, 3 }, { 4, 1, 3 } },   /* ★灯は 1 段遅らせる(体より先に灯だけが浮いて見えた) */
    { { 3, 1, 2 }, { 3, 1, 2 }, { 3, 1, 2 }, { 5, 2, 2 } },
    { { 3, 1, 2 }, { 2, 1, 2 }, { 3, 1, 2 }, { 6, 3, 2 } } };
static u8 lv, lv_t;                /* 今の段と、次に動かすまでのフレーム */

static void far_pal_set(void) {
    const u8 *p = far_pal[lv][0];
    u8 i;
    for (i = 2; i < 6; i++, p += 3) vdp_set_pal(i, p[0], p[1], p[2]);
}

/* ★1 面のオーバレイと常駐に空きが無いので、設定もここで書く: オーバレイは g_far を自分の FarCfg に向けて t1 = 0 にしておき、
   都市に入ったら埋めて置き直す(t1 = 0 の間は遠景の範囲が空なので、列の描き込みには効かない)。★降下の間は描かない(色 2..5 は
   まだ見えている雲の色)。降下の間にリングへ入った遠景の行は、印を消して描き直させる(stage.c のブロックの描き替え。色は空と同じ段 0
   から始めるので、描き直す途中は見えない) */
static u16 far_city(u16 nx) {
    FarCfg *f = g_far;
    u16 c, m = FAR_LOCK, x;
    u8 want = 0;
    s16 e;
    if (g_sec < SEC_CITY1) return nx;
    c = (u16)((g_wx >> 3) - g_w0);                            /* 画面の左の列 */
    if (!f->t1) {
        static const u8 tb[16] = CITY_FAR_TABLE;
        u8 i;
        for (i = 0; i < 16; i++) f->strip[i] = tb[i];
        f->s0 = 0; f->s1 = CITY_FAR_S1; f->t0 = 16; f->t1 = 68;
        for (i = 0; i < 32; i++) if (g_blk_tag[i] >= 16 && g_blk_tag[i] < 68) g_blk_tag[i] = 0x7FFF;
        g_pick_at = 0x7FFF;
        lv = 0; lv_t = (u8)-48; far_pal_set();                 /* 描き直しを待ってから浮かべる */
        m = FAR_RESET;
    } else {
        if ((c >= CITY_COL_CITY1 && c < CITY_COL_ARENA1 - FAR_AHEAD) || (c >= CITY_COL_CITY2 && c < CITY_COL_ARENA2 - FAR_AHEAD)) m = FAR_FREE;
        /* 見せる区間(★都市前半の頭は描き直しを待つ) */
        if ((c >= CITY_COL_CITY1 + 4 && c < CITY_COL_ARENA1 - FAR_HIDE) || (c >= CITY_COL_CITY2 && c < CITY_COL_ARENA2 - FAR_HIDE)) want = 3;
        if (g_sec <= SEC_CITY2 && ++lv_t == 12) {             /* 12 フレームに 1 段。★ボス(PLUMB は 2..5 を回す)からは書かない */
            lv_t = 0;
            if (lv != want) lv = (u8)(lv < want ? lv + 1 : lv - 1);
            far_pal_set();                                    /* 変わらなくても書き直す(やられて再開したときなど、ほかが 2..5 を書いても戻る) */
        }
    }
    x = far_core(nx | m);
    e = (s16)((nx - x) & 511u);
    if (e >= 256) e -= 512;
    f->xb = (u16)(nx - (u16)(e + (e >> 1)));
    return x;
}

/* 入口(呼び口は 1 つ: 常駐に空きが無い)。指示が FAR_CITY なら 1 面の判定から */
u16 far_step(u16 a) {
    if ((a & FAR_CITY) == FAR_CITY) return far_city(a & 0x3FFF);
    return far_core(a);
}
