/* far.c — 遠景の帯の横位置(多重スクロール。stage.c の g_far)。第 2 バンク: 常駐と 1 面のオーバレイに空きが無いので
   面で共通の計算をここに置く。面は ovl_bands で far_step(nx | 指示) を呼び、返った位置が nx とずれていれば帯の境に項目を出す。
   ・FAR_FREE: bg の動きの半分だけ進む(端数は持ち越す)
   ・FAR_LOCK: bg と一緒に動きつつ、ずれ(512 で回る)を 1/8 ずつ 0 へ戻す(真ん中に壁やボスの BG がある所の手前で)
   ・FAR_RESET: 置き直す(面の入口。ずれ 0 から)
   ・FAR_CITY: 1 面。流す・戻すをここで決める(下の far_city)
   ★本体の変数の領域に空きが無いので、指示は引数の上位 2 ビット(bg の位置は 14 ビット)、状態はここ(第 2 バンクの変数)に置く */
#include "game.h"

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

/* ---- 1 面(都市): 雲の底の下(s = 16..39)。どの短冊もこの行は空(gen_city.py。アンテナの柱は外した)。遠景は短冊 27..31 ---- */
#define CITY_ARENA1 224            /* city_data.h の CITY_COL_ARENA1 / ARENA2(★生成ヘッダは表を持つので取り込まない。gen_city.py を変えたら合わせる) */
#define CITY_ARENA2 424
/* ★1 面のオーバレイと常駐に空きが無いので、設定もここで書く: オーバレイは g_far を自分の FarCfg に向けて t1 = 0 にしておき、
   最初の呼び出しで埋めて置き直す(t1 = 0 の間は遠景の範囲が空なので、列の描き込みには効かない) */
static u16 far_city(u16 nx) {
    FarCfg *f = g_far;
    u16 c, m = FAR_LOCK;
    if (g_sec < SEC_CITY1) return nx;                         /* 降下の間は地面がまだ遠い */
    if (!f->t1) {
        u8 i;
        for (i = 0; i < 8; i++) f->strip[i] = (u8)(i < 5 ? 27 + i : 22 + i);   /* 27..31, 27..29 */
        f->s0 = 0; f->s1 = 26; f->t0 = 16; f->t1 = 40;
        m = FAR_RESET;
    } else {
        c = (u16)(((g_wx >> 3) - g_w0) + 48);                 /* 画面の右の 16 列先 */
        if (g_sec == SEC_CITY1 && c < CITY_ARENA1) m = FAR_FREE;
        else if (g_sec == SEC_CITY2 && c < CITY_ARENA2) m = FAR_FREE;
    }
    return far_core(nx | m);
}

/* 入口(呼び口は 1 つ: 常駐に空きが無い)。指示が FAR_CITY なら 1 面の判定から */
u16 far_step(u16 a) {
    if ((a & FAR_CITY) == FAR_CITY) return far_city(a & 0x3FFF);
    return far_core(a);
}
