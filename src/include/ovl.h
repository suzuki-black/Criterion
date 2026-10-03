/* ovl.h — オーバレイ(場面ごとのコードの差し替え。指示書 8.12「ボスごとの処理は RAM オーバレイで差し替え」)。
   ページ 0 の RAM の OVL_BASE..OVL_END(CODE0 の後ろ)に、ROM から場面のコードを読み込んで実行する。
   ・オーバレイは本体とは別にリンクする(tools/gen_ovl.py)。本体の関数・変数は本体の .noi から絶対番地で解決する。
   ・本体からオーバレイを呼ぶときは、オーバレイの先頭の JP 表(3 バイト × 番号)を通る。番号ごとの名前:
       タイトル(OVL_TITLE): title_load / title_start / title_update / title_draw / title_vram / title_build
       面(OVL_S1..): ovl_init / ovl_update / ovl_logic / ovl_draw / ovl_vram / ovl_bands
     ★名前が違っても番地は同じ。今読み込まれているオーバレイの関数だけを呼ぶこと(g_ovl / g_ovl_ready)。
   ・オーバレイの変数(static を含む)は OVL_DATA に置かれる(初期値つきの変数は使えない: crt0 が初期化しない。
     gen_ovl.py が検査する)。
   ・ROM の置き場: バンク OVL_BANK0 + (id - 1) × 2(1 つ 16KB)。 */
#ifndef OVL_H
#define OVL_H

#include "types.h"

#define OVL_BASE   0x0A00
#define OVL_END    0x3800
#define OVL_SIZE   (OVL_END - OVL_BASE)
#define OVL_DATA   0xD000              /* 〜0xD7FF */
#define OVL_BANK0  120

enum { OVL_NONE, OVL_TITLE, OVL_S1, OVL_S2, OVL_S3, OVL_S4 };

extern u8 g_ovl;                       /* 読み込まれているオーバレイ */
extern u8 g_ovl_ready;                 /* 1 = 読み終わって呼んでよい */
void ovl_load_sync(u8 id);             /* すぐに全部読む(表示を止めているとき) */
void ovl_request(u8 id);               /* 少しずつ読む(ovl_step を毎フレーム。読み終えたら面のものは ovl_init) */
void ovl_step(void);

/* 面のオーバレイの入口(JP 表の順) */
void ovl_init(void);                   /* 読み終えたとき */
void ovl_update(void);                 /* stage_update の中で(区間を進める) */
void ovl_logic(void);                  /* 敵の更新の後(ボス) */
void ovl_draw(void);                   /* スプライトの段 */
void ovl_vram(void);                   /* VRAM の窓 */
void ovl_bands(s16 sc, u16 nx);        /* ラスターリスト: 地面(s >= 0)の帯。stage_ev_scroll / stage_ev_pal で */

#endif /* OVL_H */
