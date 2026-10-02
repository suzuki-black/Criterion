/* game.h — ゲーム本体(M1: 1 面の空中戦)の共通定義。
   座標は 1/16 ドット単位の固定小数点(s16)。プレイエリアは 256 x PLAY_H(その下は HUD 帯 24 行)。 */
#ifndef GAME_H
#define GAME_H

#include "types.h"
#include "raster.h"

#define FX(v)     ((s16)((v) * 16))     /* ドット → 固定小数点 */
#define PX(v)     ((s16)((v) >> 4))     /* 固定小数点 → ドット */
#define PLAY_H    188
#define HUD_Y     188                 /* HUD 帯の画面の行 */
/* ★HUD はページ 2 の 212..235 行に描き、HUD 帯だけ R#2 = 0x7F(ページ 2|3)・R#23 = 24 で見せる
   (表示リングは縦スクロールで 256 行すべてが画面に出るので、HUD をリングに置けない。M2) */
#define HUD_PAGE  2
#define HUD_VROW  212                 /* HUD の VRAM の行(ページ 2) */
#define HUD_R23   24                  /* = HUD_VROW - HUD_Y */
/* HUD の文字の行(ページ 2 の VRAM の行)。★SDCC は「212 + 8」のような定数式を u8 の引数に渡すと
   値が範囲内でも警告するので、足した値で持つ */
#define HUD_ROW0  212
#define HUD_ROW1  220
#define HUD_ROW2  228

/* ---- ∞ゲージ(指示書 2.3) ---- */
enum { N_INF, N_THRUST, N_ECHO, N_AEGIS, N_INF2, N_DROP, N_SPLIT, N_RAY, N_STEPS };
typedef struct {
    u8 cursor;          /* 0..7(巡回順。0 と 4 が ∞) */
    u8 armed;           /* 0 = 確定直後の休止(∞で B を押しても LIMIT BREAK しない)/ 1 = Quanta で進めた */
    u8 anim;            /* カーソル光点の移動アニメ(残りの段数) */
    u8 thrust, echo, aegis, drop, split, ray;   /* 各強化の段階 */
    u8 flash;           /* 確定の演出の残りフレーム */
} Gauge;
extern Gauge g_gauge;

/* ---- 自機 ---- */
typedef struct {
    s16 x, y;           /* 左上(固定小数点) */
    u8  alive;
    u8  invul;          /* 無敵の残りフレーム(復活直後) */
    u8  fire_cd;        /* 次のショットまで */
    u8  dead_t;         /* やられてから復活までのフレーム */
    u8  lives;
    u16 score;          /* 10 点単位(表示で 0 を 1 つ足す) */
} Player;
extern Player g_pl;

/* ---- 敵・弾・アイテム・爆発(固定数のプール) ---- */
enum { E_NONE, E_TICK, E_TOCK, E_NOTCH, E_SPIRIT };
typedef struct {
    u8  type;
    u8  t;              /* 出現からのフレーム */
    s16 x, y, vx, vy;
    s16 y0;             /* 正弦波の中心 */
    u8  phase;
    u8  form;           /* 編隊の番号(0xFF = 単独) */
    u8  hp;
    u8  turned;         /* Notch: 旋回済み */
} Enemy;
#define MAX_ENEMY 16
extern Enemy g_enemy[MAX_ENEMY];

typedef struct { s16 x, y, vx, vy; u8 on; u8 pierce; } Bullet;
#define MAX_PSHOT 12
#define MAX_ESHOT 16
extern Bullet g_pshot[MAX_PSHOT], g_eshot[MAX_ESHOT];

typedef struct { s16 x, y, vx, vy; u8 on; u8 kind; u8 t; } Item;   /* kind 0 = Quanta, 1 = Infinity Shard */
#define MAX_ITEM 8
extern Item g_item[MAX_ITEM];

typedef struct { s16 x, y; u8 t; } Fx;
#define MAX_FX 10
extern Fx g_fx[MAX_FX];

/* ---- 共通の小物 ---- */
extern u16 g_frame;                 /* ゲーム開始からのフレーム */
extern const s8 g_sin64[64];        /* 正弦(±127) */
u8   rnd8(void);
void fx_spawn(s16 x, s16 y);
void item_spawn(s16 x, s16 y, u8 kind);
void score_add(u16 v10);            /* 10 点単位 */

extern u8 g_vscroll;                 /* プレイエリアの縦スクロール(R#23)。bg.c */

/* 各モジュール */
void bg_init(void);
void bg_scroll(void);               /* 1 フレーム分スクロールを進める */
void bg_mix(u8 t);                  /* 空と雲の色: 0 = 夜 〜 16 = 夕焼け */
const u8 *bg_sky(u8 band);          /* 空の帯(0..6)の今の色 R, G, B */
u16  bg_layer_x(u8 l);              /* 雲の層(0 遠 / 1 中 / 2 近)の横位置 */
extern u8 g_scroll_speed;           /* 1/4 ドット/フレーム単位 */
void bg_fill_hud(RasEntry *e, u8 bi);   /* HUD 帯の項目(188 行)を作る */

/* ---- 場面(指示書 4: 一本のフィルム。暗転しない) ---- */
enum { PH_LOGO, PH_TITLE, PH_SORTIE, PH_AIR, PH_COUNT };
extern u8  g_phase;
extern u16 g_pt;                    /* 今の場面に入ってからのフレーム */
extern u8  g_hud_on;                /* 1 = ゲームの HUD を描いた(出撃で上の帯が消えてから) */
extern u8  g_split_shown;           /* 今表示しているリストを作ったときの g_split_n */
extern u8  g_split_n;               /* 上の SCREEN12 の帯の高さ(0 = 分割なし)。VRAM の書き込みはその下の区間で */
void title_load(void);              /* タイトルの素材を ROM から VRAM へ(全面 SCREEN5 のときに) */
void title_update(void);            /* 起動ロゴ・タイトル・出撃の進行(場面を進める) */
void title_draw(void);              /* その場面のスプライト */
void title_vram(void);              /* その場面の VRAM の仕事(下の区間で) */
void title_build(void);             /* その場面のラスターリスト */
void title_start(void);             /* 起動ロゴから始める */
void game_start(void);              /* 自機・敵・ゲージを最初の状態に(main.c) */
void hud_init(void);
void hud_update(void);              /* 変わったところだけ書く */
void hud_sprites(void);             /* ゲージのカーソル光点 */
void gauge_quanta(void);            /* Quanta を取った */
void gauge_shard(void);             /* Infinity Shard を取った(カーソルを ∞ へ) */
void gauge_confirm(void);           /* B: 確定 */
void gauge_lose(void);              /* 被弾: 強化を全部失う */
void player_init(void);
void player_update(void);
void player_draw(void);
void player_hit(void);
void enemy_init(void);
void enemy_update(void);
void enemy_draw(void);
void limit_break(void);             /* ∞: 画面の敵と弾を一掃 */
enum { WAVES_NONE, WAVES_AIR, WAVES_CITY };
void enemy_waves(u8 set);           /* 出現の台本を区間に合わせて切り替える(時刻は 0 から) */
void enemy_spawn_spirit(u16 wx, u8 roof);

/* ---- 面の進行と縦に長い世界(stage.c) ----
   縦の座標 s: 都市の画面の行と同じ(0 = 雲の底、158 = 歩道)。空中戦の空と雲は s = -188..-1。
   表示リングの行 = (188 + s) & 255、R#23 = (188 + g_scam) & 255。g_scam = 画面の上端の s。 */
enum { SEC_AIR1, SEC_DESCENT, SEC_CITY1, SEC_MID, SEC_CITY2, SEC_BOSS, SEC_ASCENT, SEC_AIR2 };
extern u8  g_sec;                   /* 今の区間 */
extern u16 g_wx;                    /* 世界の横位置(近い層のドット。画面の左端) */
extern s16 g_scam;                  /* 画面の上端の s */
void stage_start(void);             /* 1 面の空中戦から(PH_AIR に入るとき) */
void stage_update(void);            /* 区間を進める(スクロールの後) */
void stage_vram(void);              /* 列と行の描き込み(VRAM の窓で) */
void stage_build(void);             /* ラスターリスト */

#endif /* GAME_H */
