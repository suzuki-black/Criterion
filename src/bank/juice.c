/* juice.c — 撃破の演出(M7 の後、指摘 C4。第 2 バンク: 常駐の空きのため本体から移した): 中ボス = ちょっとした破壊、面ボス = 派手、ラスボス = やり過ぎなくらい。
   ボスが BS_DYING になった区間で段階を決め、画面の揺れ(R#23 に足す g_roll)・閃光(色 0)・爆発の追加・爆発音を出す。
   ★区間の番号は各面の sN.h の enum と合わせる(本体は面のヘッダを取り込まないので数で持つ) */
#include "game.h"
#include "vdp.h"
#include "snd.h"

static const u8 boss_sec[] = { 5, 13, 22, 31, 41, 50, 53, 60, 61 };   /* 1〜6 面のボス、7 面 LIBRA、8 面の第 1・第 2 形態 */
#define SEC_LAST 62                                                   /* 8 面の第 3 形態(SEC8_BOSS3) */
static u8 juice_t, juice_tier, juice_len, bs_last, shard_t, shard_y, jc_ok;   /* ★第 2 バンクの変数は初期化されないので最初に代入(jc_ok) */
void juice_frame(void) {
    u8 s = g_boss_state;
    if (jc_ok != 0x5A) { juice_t = 0; bs_last = s; shard_t = 0; jc_ok = 0x5A; }
    if (s == BS_DYING && bs_last != BS_DYING) {
        u8 i;
        juice_tier = 1;
        if (!g_calib) {
            if (g_sec == SEC_LAST) juice_tier = 3;
            else for (i = 0; i < sizeof boss_sec; i++) if (boss_sec[i] == g_sec) juice_tier = 2;
        }
        juice_len = juice_t = juice_tier == 1 ? 45 : juice_tier == 2 ? 120 : 255;
        if (juice_tier == 2 || g_calib) {                     /* 面のボス・試験のボス: Infinity Shard(指示書 2.3「ボス撃破時のみ出現」) */
            shard_y = (u8)(g_boss_aim > 16 && g_boss_aim < PLAY_H - 16 ? g_boss_aim : 80);
            shard_t = juice_tier == 2 ? 112 : 1;              /* ★面ボスは部分拡大が終わってから出す(拡大の帯で 2 倍に見えた) */
        }
    }
    bs_last = s;
    g_mag = 0;
    if (shard_t && !--shard_t) item_spawn(FX(160), FX(shard_y), 1);
    if (!juice_t) return;
    juice_t--;
    /* ★面ボスとラスボスは、自機のいない帯のスプライトを 2 倍に(爆発が大きくなる。部分拡大、C5)。境は 96 行 */
    if (juice_tier >= 2 && juice_t > 8) { u8 py = (u8)(g_pl.y >> 4); if (py >= 96) g_mag = 1; else if (py < 78) g_mag = 2; }
    {   /* 揺れ: 段階で幅を変え(±2 / ±4 / ±8)、終わりへ向けて小さく(★毎フレームの経路なので % を使わない) */
        u8 m = (u8)(juice_tier == 1 ? 3 : juice_tier == 2 ? 7 : 15);
        if (juice_t < 10) m >>= 2; else if (juice_t < 20) m >>= 1;
        g_roll = juice_t ? (u8)((rnd8() & m) - (m >> 1)) : 0;
    }
    {   /* 閃光: 中ボス 1 回、面ボス 3 回、ラスボスは何度も(最後は白く飛ぶ) */
        u8 e = (u8)(juice_len - juice_t), on;
        if (juice_tier == 1) on = e < 2;
        else if (juice_tier == 2) on = e < 3 || (e >= 30 && e < 32) || (e >= 70 && e < 72);
        else on = (e < 4) || !(e & 15) || (juice_t < 24 && juice_t > 8);
        if (on) vdp_set_pal(0, 7, 7, 7); else vdp_set_pal(0, 0, 0, 0);
    }
    /* ★爆発を足すのはラスボスだけ(爆発は G_MISC の 4 枠を ∞ と取り合って枠の色を書き直すので重く、処理落ちで
       撃破が長引いた。ボスは撃破の間それぞれ爆発を出している) */
    if (juice_tier == 3 && !(juice_t & 7)) fx_spawn(FX(64 + (rnd8() & 127)), FX(24 + (rnd8() & 127)));
    if (juice_tier >= 2 && !(juice_t & 31)) sfx(SFX_BOOM);
}
