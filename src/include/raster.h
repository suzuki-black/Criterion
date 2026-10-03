/* raster.h — ラスターリスト(指示書 8.4)。走査線割込み(R#19 / S#1 FH)で帯ごとに VDP を書き換える。

   ★データ駆動・ダブルバッファ: メインは ras_back() で裏の表を受け取って組み、ras_commit(n) で確定する。
     VBLANK 割込みが表を差し替え、先頭(line==0)の項目を適用し、次の分割行を R#19 へ仕込む。
     (分割の仕切り直しは必ず VBLANK 文脈で行う。前作で、本体側で仕切り直して先頭の分割を
      毎フレーム取りこぼした。)
   ★HR ポーリング帯(陽炎・重力レンズ): flags に RAS_HAZE を立てた項目は、割込みの中で S#2 の HR を
     見ながら n ラインぶん毎ライン R#26/R#27 を書く。その間はメインが止まる(帯の高さぶんを予算から引く)。

   ★割込みの入口は 2 方式(M0-2 で比較する):
     RAS_ISR_BIOS   … BIOS の割込みハンドラ → H.KEYI(0xFD9A) から呼ばれる(前作と同じ)。
     RAS_ISR_DIRECT … ページ 0 を RAM に切り替え、0x0038 に JP を置いて直接受ける。BIOS の
                      全レジスタ退避とキー走査が無くなるぶん応答が速い。この方式の間は BIOS を
                      呼んではいけない(JIFFY は自前で進める)。
   ★作法(前作 raster.h から):
     - S#1 を読むため R#15=1 にしたら、必ず R#15=0 へ戻す。
     - R#19 は「表示の行 + R#23」で比べられる。R#23 は RAS_REGS で書き(ras_vs に覚える)、以降の項目の R#19 に足す。
     - 割込みの応答遅れのぶん、R#19 は line - g_ras_bias に仕込む(実測で決める)。 */
#ifndef RASTER_H
#define RASTER_H

#include "types.h"

#define RAS_MAX 20
#define RAS_GAP 6          /* 項目どうしの最小の間隔(ライン)。これより近い項目は割込みを待たずに続けて適用される */

#define RAS_SCROLL 0x01   /* R#26/R#27 を書く */
#define RAS_PAL    0x02   /* パレット 1 色を書く */
#define RAS_HAZE   0x04   /* HR ポーリング帯: n ライン、haze 表の (r26,r27) を毎ライン書く */
#define RAS_HAZE_FINE 0x08 /* HR ポーリング帯を R#27 だけにする(帰線での書き込み 1 回。振幅は 0..7 ドット) */
#define RAS_MARK   0x20   /* 適用されたら g_ras_mark を +1(メインが「この行を過ぎた」を待つため) */
#define RAS_REGS   0x10   /* レジスタ列: haze を (番号, 値) の並び、haze_n を組数として順に書く(RAS_HAZE とは排他) */

typedef struct {
    u8  line;        /* 画面行(0..211)。0 は VBLANK 中に適用(フレーム先頭の状態)。昇順に並べる */
    u8  flags;
    u8  r26, r27;
    u8  pidx;        /* パレット番号 */
    u8  prb, pg;     /* (R<<4)|B, G */
    u8  haze_n;      /* HR ポーリングするライン数(RAS_REGS では組数) */
    const u8 *haze;  /* (r26, r27) × haze_n(RAS_REGS では (番号, 値) × haze_n) */
} RasEntry;          /* 10 バイト(ISR が固定オフセットで読む) */

#define RAS_ISR_BIOS   0
#define RAS_ISR_DIRECT 1

extern volatile u8 g_ras_bias;   /* 割込み応答遅れの補正(ライン) */
extern volatile u8 g_ras_busy_border;
extern volatile u16 g_ras_irqs, g_ras_fh, g_ras_vb;   /* 割込み・FH・VBLANK の累計(計測用) */
extern volatile u8  g_ras_mark;     /* RAS_MARK の項目が適用されるたびに +1 */
extern volatile u16 g_ras_rearm;    /* PCM の刻みの鎖を VBLANK で張り直した回数(計測用。0 が正常) */  /* 0 以外: ISR の間だけ枠線色をこの番号にする(負荷可視化) */

void      ras_init(void);            /* H.KEYI フック設置、E1 有効(BIOS 方式で開始) */
RasEntry *ras_back(void);            /* 裏の表(書き込んでよい) */
u8        ras_back_index(void);      /* 裏の表の番号 0/1(付随データのダブルバッファ用) */
void      ras_commit(u8 n);          /* 裏の表を n 項目で確定。次の VBLANK で表になる */
u8        ras_set_isr(u8 mode);      /* 割込みの入口を切り替える。戻り値 = 実際のモード */

/* ---- PCM(指示書 8.6 の方式 A/C。直接方式で使う) ----
   再生中は「刻み方式」: 走査線割込みを 2 ラインごとの固定の刻みにして 1 サンプルずつ出す(15734/2 = 7867Hz)。
   ラスターの項目はその刻みまでに来ているものを適用する(最大 1 ライン早まる)。VBLANK 割込みは止め、
   VBLANK の仕事(JIFFY・表の差し替え)は刻み 212 で行う。詳細は raster.c と docs/M0_技術検証.md(M0-3)。
   fill=0(方式 A): 割込みが起きない 246..261 行と HR ポーリング帯の間は出さない(その分だけ再生が伸びる)。
   fill=1(方式 C): その間も HR を数えて出す。M0-3 で方式 C に決定。
   開始は次の VBLANK から(フレームの途中から割り込ませるとラスターの順番を乱すため)。
   ★刻みは表示ラインで数え、R#19 = 刻み + R#23 で仕込む(帯で R#23 が変わっても鎖が切れない。M7)。
   ★pcm_stop は割込みの刻み 212 で項目方式へ戻す(本体から VBLANK 割込みを戻すと JIFFY を 2 回数える。M7)。 */
extern void (*volatile ras_pcm_hook)(void);   /* PCM 方式 B の送出口(割込みの入口と HR ポーリング帯で呼ぶ。0 = なし) */
extern volatile u8 ras_pstep, ras_pmask;      /* 刻みの幅(2 = 7.9kHz、4 = 3.9kHz)と幅 - 1。PCM を鳴らす前に決める */
void pcm_play(const u8 *p, u16 n, u8 fill);   /* 再生中に呼ぶと今のデータを差し替える */
void pcm_stop(void);                          /* 刻み 212 で止まる(それまでは何も出さない) */
void pcm_queue(const u8 *p, u16 n);           /* 今のデータが鳴り終わったら、切れ目なく続けて鳴らす */
u8   pcm_queued(void);                         /* 予約が残っていれば 1 */
u16  pcm_remaining(void);

#endif /* RASTER_H */
