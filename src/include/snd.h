/* snd.h — 音(指示書 6): MSX-MUSIC(OPLL)6 声 + リズム 5 音 + PSG 3 声の BGM ドライバ、PSG の効果音、PCM の再生。

   ★本体のループから毎フレーム snd_frame() を呼ぶ(割込みの中では鳴らさない。OPLL の書き込みは 1 回約 25us で、
     ISR に置くとラスターの項目を押し出す。前作 §16-39)。テンポは JIFFY(60Hz)で数えるので、処理落ちで
     フレームが遅れても曲の速さは変わらない(遅れたぶんは次のフレームでまとめて進める)。
   ★曲データは ROM(tools/gen_sound.py が作る snd.bin)。読むときだけページ 2 をカートリッジにして曲のバンクを
     出す(ページ 1 は RAM のまま)。その間はページ 2 のもの(コード・const)を触れないので、snd.c はページ 1 に置く
     (Makefile が検査)。
   ★PCM は ROM から RAM(ページ 0 の 0x3D00..0x3FFF、384 バイト × 2)へ毎フレーム流し込み、刻み方式(raster.c、
     方式 C)で鳴らす。同時 1 音、優先度制(音声 > オケヒ > 爆発 > スネア)。
   ★チャンネル: 0..5 = FM、6 = リズム、7..9 = PSG A/B/C。効果音は PSG C を使い、鳴っている間は BGM の C が譲る。 */
#ifndef SND_H
#define SND_H

#include "types.h"

#define SND_NCH   10
#define SND_NONE  0xFF

/* 効果音(PSG C) */
enum { SFX_SHOT = 1, SFX_HIT, SFX_KILL, SFX_BOOM, SFX_ITEM, SFX_SHARD, SFX_CONFIRM, SFX_LIMIT, SFX_DIE,
       SFX_TICK, SFX_DENY, SFX_COUNT };

void pcmx_service(void);      /* PCM 方式 B の送出口(鳴っていなければすぐ帰る。重いループに置く) */
extern u8  g_opll;            /* 1 = MSX-MUSIC がある(turboR は内蔵) */
extern u8  g_bgm;             /* 今の曲(SND_NONE = 無し) */
extern s8  g_snd_trans;       /* 全体の移調(半音。新しい音から) */
extern s8  g_snd_bend;        /* ピッチベンド一括(半音。鳴っている音もすぐ変わる) */
extern s8  g_snd_detune;      /* 細かいゆらぎ(FM の F-Number / PSG の周期に足す。鳴っている音もすぐ変わる) */
extern u8  g_snd_tempo;       /* テンポの外部制御(128 = 曲どおり) */
extern u8  g_snd_atten;       /* 全体の減衰(0 = そのまま .. 15 = 無音) */
extern u16 g_snd_mute;        /* チャンネルの消音(bit = チャンネル番号) */
extern u8  g_snd_bar;         /* 小節の頭(曲の CUE)ごとに +1 */
extern u16 g_snd_keyons;      /* 発音の累計(検証用) */
extern u16 g_pcm_starts;      /* PCM を鳴らし始めた回数(検証用) */
extern u8  g_pcm_cur;         /* 鳴っている PCM(SND_NONE = 無し) */

void snd_init(void);          /* 音源の初期化(消音) */
void bgm_play(u8 id);         /* すぐ切り替える(SND_NONE = 止める) */
void bgm_next(u8 id);         /* 次の小節の頭で切り替える */
void bgm_fade(u8 id, u8 step);   /* 今の曲をフェードアウトしてから切り替える(step フレームごとに 1 段、15 段) */
void bgm_stop(void);
void sfx(u8 id);
void pcm_req(u8 id);          /* PCM を鳴らす(鳴っているものより優先度が低ければ無視)。声など必ず鳴らすもの */
void pcm_req_opt(u8 id);      /* 任意の PCM(オケヒ・爆発など): 処理落ちしていない時間が 1 秒続いているときだけ */
void pcm_halt(void);          /* PCM を止める(重い読み込みの前など) */
void snd_frame(void);         /* 毎フレーム 1 回(本体のループ) */
void pcm_keep(void);          /* PCM の補充だけ(snd_frame を先送りしたフレーム) */
void snd_silence(void);       /* 全部止める(BGM・効果音・PCM) */
void music_frame(u8 over);    /* music.c(CODE0): 場面に合わせて曲を選ぶ。over = ゲームオーバー */

#endif /* SND_H */
