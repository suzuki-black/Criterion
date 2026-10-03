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
extern u8  g_slow;                  /* 1 = このフレームは処理落ち(敵・ボス・Pawn の更新を飛ばす。main.c) */
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
extern s8 g_scroll_speed;           /* 1/4 ドット/フレーム単位(負 = 逆走。3 面) */
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
extern u8 g_wave_mirror;            /* 1 = 編隊を左から出す(3 面の逆走) */
void enemy_spawn_spirit(u16 wx, u8 roof);
void enemy_bullet(s16 x, s16 y, s16 vx, s16 vy);   /* 固定小数点 */
void enemy_shoot(s16 x, s16 y);     /* 自機を狙う */

/* ---- ボスの状態(本体に置く。面のオーバレイが動かす) ---- */
enum { BS_NONE, BS_ENTER, BS_FIGHT, BS_DYING, BS_DONE };
extern u8  g_boss_state;
extern u8  g_boss_hp;
extern u8  g_boss_dmg;              /* 受け取っていない傷(boss_damage が足す) */
extern u16 g_lg_on[26];             /* LEGION の積んであるマス(検証も読む) */
extern s16 g_lg_x0;
void boss_damage(u8 n);             /* LIMIT BREAK など: 次のフレームで面が受け取る */

/* ---- 重力(2 面。指示書 5.2)。自機・敵弾・アイテムに効く ----
   g_grav: GRAV_NONE / GRAV_DIR(一様: g_gx, g_gy。1/16 ドット/フレーム² 単位)/ GRAV_CENTER(画面の (g_gcx, g_gcy)
   へ、距離に比例: 1 ドット離れるごとに 1/512 ドット/フレーム²)。g_pl_ymin / g_pl_ymax: 自機の上下の限り(地形) */
enum { GRAV_NONE, GRAV_DIR, GRAV_CENTER };
extern u8  g_grav;
extern s16 g_gx, g_gy, g_gcx, g_gcy;
extern s16 g_pl_ymin, g_pl_ymax;    /* ドット(自機の左上の y) */
extern u8  g_roll;                  /* 画面の縦のロール(R#23 に足す。スプライトは動かさない) */
/* ---- ∞ の紋章(infx.c。ROTOR のスプライト用) ---- */
void infx_start(s16 x, s16 y);      /* 確定の演出: (x, y) から画面の中央へ */
void infx_title(u8 on);             /* タイトルの紋章 */
void infx_reset(void);
void infx_update(void);             /* 論理の段 */
void infx_vram(void);               /* VRAM の窓 */
void infx_draw(void);               /* スプライトの段 */
/* ---- 面の進行と縦に長い世界(stage.c) ----
   縦の座標 s: 地面の区間の画面の行と同じ(1 面の都市: 0 = 雲の底、158 = 歩道)。空中戦の空と雲は s = -188..-1。
   表示リングの行 = (188 + s) & 255、R#23 = (188 + g_scam) & 255。g_scam = 画面の上端の s。 */
enum { SEC_AIR1, SEC_DESCENT, SEC_CITY1, SEC_MID, SEC_CITY2, SEC_BOSS, SEC_ASCENT, SEC_AIR2 };
extern u8  g_sec;                   /* 今の区間 */
extern u16 g_wx;                    /* 世界の横位置(近い層のドット。画面の左端) */
extern s16 g_scam;                  /* 画面の上端の s */
void stage_start(void);             /* 1 面の空中戦から(PH_AIR に入るとき) */
void stage_update(void);            /* 区間を進める(スクロールの後) */
void stage_vram(void);              /* 列と行の描き込み(VRAM の窓で) */
void stage_build(void);             /* ラスターリスト */
void stage_pump_nb(void);           /* VDP が空いていれば列の描き込みと GHOST を出す(待たない) */
/* 面のオーバレイから使う */
#define MAP_MAX 640
enum { UP_AIR, UP_SKY };            /* s < 0 の中身: 空中戦の空と雲(起動時の絵)/ 高い空(塗って星) */
extern s16 g_scam_q;                /* g_scam × 16 */
extern u8  g_sky_t;                 /* 高い空の暗さ 0..16 */
extern u16 g_w0;                    /* 地図の列 0 の世界の列 */
extern u8  g_map[MAP_MAX];
extern u16 g_maplen;
void stage_set_upper(u8 kind);
void stage_load_strips(u8 bank, u16 off);   /* 短冊(8x188 × 32 本、24064 バイト)を少しずつページ 3 へ */
void stage_reset_ground(void);              /* 地面を捨てる(次の面の短冊を読む前。地面が画面に出ていないとき) */
extern u8 g_col_rev;                        /* 列の窓を左へずらす列数(逆走に備える。31 = 逆走、16 = 前後に揺れる。3 面) */
extern u8 g_strip_xlat[32];                 /* 短冊の番号の置き換え(描くときに引く。3 面の高速の流線) */
u8   stage_redraw_col(u16 wcol);            /* 世界の列 wcol を描き直す(地図を書き換えたとき。隔壁の開閉)。0 = 頼みがいっぱい */
u8   stage_busy(void);                       /* 列の流し込み・描き替えが残っている(画面の BG に上書きする前に待つ) */
void stage_banner(const char *text);        /* 面の開始表示 */
void stage_ev_scroll(s16 line, u16 x);      /* ラスターリスト(ovl_bands の中で、行の順に) */
void stage_ev_pal(s16 line, const u8 *rgb);
void stage_ev_haze(s16 line, u8 n, u8 *tbl);  /* HR ポーリング帯(R#27 を毎ライン。tbl は (r26, r27) × n) */
void ring_cmd(u16 sx, u16 sy, s16 x, s16 y, u8 w, u8 h, u8 fill, u8 byte);   /* 画面の矩形 → 表示リング(GHOST) */

#endif /* GAME_H */
