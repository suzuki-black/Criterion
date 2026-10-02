/* game.h — ゲーム本体(M1: 1 面の空中戦)の共通定義。
   座標は 1/16 ドット単位の固定小数点(s16)。プレイエリアは 256 x PLAY_H(その下は HUD 帯 24 行)。 */
#ifndef GAME_H
#define GAME_H

#include "types.h"

#define FX(v)     ((s16)((v) * 16))     /* ドット → 固定小数点 */
#define PX(v)     ((s16)((v) >> 4))     /* 固定小数点 → ドット */
#define PLAY_H    188
#define HUD_Y     188
/* HUD の文字の行。★SDCC は「188 + 8」のような定数式を u8 の引数に渡すと値が範囲内でも警告するので、足した値で持つ */
#define HUD_ROW0  188
#define HUD_ROW1  196
#define HUD_ROW2  204

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
enum { E_NONE, E_TICK, E_TOCK, E_NOTCH };
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

/* 各モジュール */
void bg_init(void);
void bg_scroll(void);               /* 1 フレーム分スクロールを進める */
void bg_build(void);                /* ラスターリストを組む(spr_end の後) */
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

#endif /* GAME_H */
