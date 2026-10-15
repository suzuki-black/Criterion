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
/* HUD 帯の項目の割込みの行(RAS_BLANK で 186・187 行を消して切り替える。プレイエリアの項目はこの RAS_GAP 手前まで) */
#define HUD_IRQ   186
/* プレイエリアの項目はこの行より前に(★HUD の項目の手前の項目の割込みが長引くと、HUD の切り替えが 1 行ずれた) */
#define ITEM_END  (HUD_IRQ - RAS_GAP - 3)
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
enum { E_NONE, E_TICK, E_TOCK, E_NOTCH, E_SPIRIT,
       E_BK = 8,                  /* ここから先は第 2 バンクの zako.c が動かして描く(指示書 3 章) */
       E_CALIPER = E_BK, E_HATCH, E_CRAFT, E_DIAL, E_PLUMMET, E_PROTR, E_COMPASS, E_ABACUS, E_SEXTANT, E_SMITE, E_SCANNER };
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
/* 第 2 バンクの雑魚(zako.c)が使う enemy.c の口 */
Enemy *enemy_alloc(void);
void enemy_kill(Enemy *e, u8 by_player);
void enemy_wave(u8 type, s16 y0, s8 dir);  /* Tick / Tock / Notch の編隊 */
u8   enemy_new_form(void);
extern u8 g_form_left[];
extern u16 g_wave_t;               /* 台本の時刻(0..1199) */
extern u8 g_wave_set, g_wave_mirror;   /* 今の台本(WAVES_*)・逆走(左から入る) */
extern u8 g_zako_n;                /* 生きている第 2 バンクの雑魚の数(0 なら zako_update / zako_draw を呼ばない) */
u8   zako_script(void);            /* 第 2 バンク: 面ごとの台本(1 = 引き受けた) */
void zako_update(void);
void zako_draw(void);
void zako_killed(Enemy *e);        /* 自機に倒された(砂時計の砂など) */

typedef struct { s16 x, y, vx, vy; u8 on; u8 pierce; } Bullet;
#define MAX_PSHOT 16                /* ふつうの弾 12 + DROP の爆雷 4。★20 にするとボスの当たり判定(部品ごとに全部の弾を見る)が
                                               重くなり、1 周の処理落ちが 2 → 4 になった。足りない枠は、撃つ機ごとに正面の 1 発を先に取り、拡散は順番を回して分ける(weapon.c) */
#define MAX_ESHOT 16
extern Bullet g_pshot[MAX_PSHOT], g_eshot[MAX_ESHOT];
/* 自機の弾の on: 1 = ふつうの弾(player.c が進める)、2 / 3 = DROP の爆雷の下向き / 上向き(weapon.c が進める) */
#define PS_DROP_DN 2
#define PS_DROP_UP 3
#define PS_SHOTS   (MAX_PSHOT - 4)  /* ふつうの弾が使う枠(最後の 4 つは爆雷専用。分身の弾で埋まって爆雷が出せなかった) */

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
/* ★SCREEN12(GRAPHIC7)の帯は SCREEN5 より表示が右へ 4 ドットずれる(openMSX で測定: 表示の左右の端が 40..287 → 44..291)。
   上の帯の間だけ R#18 の横の調整を左へ 4 にし、SCREEN5 へ戻す所で元の値(RG18SAV)に戻す(遊んでの指摘「上と下で x の始まりが違う」) */
#define R18_SAVE   (*(volatile u8 *)0xFFF1)
#define R18_G7     ((u8)((R18_SAVE & 0xF0) | ((R18_SAVE + 4) & 0x0F)))
void title_load(void);              /* タイトルの素材を ROM から VRAM へ(全面 SCREEN5 のときに) */
void title_update(void);            /* 起動ロゴ・タイトル・出撃の進行(場面を進める) */
void title_draw(void);              /* その場面のスプライト */
void title_vram(void);              /* その場面の VRAM の仕事(下の区間で) */
void title_build(void);             /* その場面のラスターリスト */
void title_start(void);             /* 起動ロゴから始める */
void game_start(void);              /* 自機・敵・ゲージを最初の状態に(main.c) */
void hud_init(void);
void hud_update(u8 rows);           /* 変わったところだけ書く(1 フレームに rows 行) */
void hud_sprites(void);             /* ゲージのカーソル光点 */
void gauge_quanta(void);            /* Quanta を取った */
void gauge_shard(void);             /* Infinity Shard を取った(カーソルを ∞ へ) */
void gauge_confirm(void);           /* B: 確定 */
void gauge_lose(void);              /* 被弾: 強化を全部失う */
/* ---- 面の開始表示(指示書 4.2): HUD 帯の上に 16 行の帯を出し、「STAGE 2 / GRAVITY WELL」を左から滑り込ませ、
   2 秒で HUD 帯へ吸い込ませる(帯が上から縮み、文字が上から消える)。stage_banner が文字を写し、VRAM の仕事は
   第 2 バンクの banner.c、ラスターの帯は stage.c。 */
typedef struct {
    u8 t;              /* 進み(0 = なし) */
    u8 x, fg;          /* 文字の左端・色(7 面の判定文は赤く崩れていく) */
    u8 p3;             /* 1 = ページ 3 を塗って横スクロールで滑り込ませる(短冊を使っていないとき) */
    u16 bx;            /* 横の位置(256 → 0) */
    u16 cp;            /* 短冊を使っているとき: 帯へ写した幅 */
    char text[32];     /* ★写しを持つ(呼ぶのは古いオーバレイで、その文字列は次のオーバレイの読み込みで消える) */
} Banner;
extern Banner g_ban;
#define BAN_LINE  172
#define BAN_ROW   168              /* (BAN_LINE + 252) & 255 */
#define BAN_IN    7                /* ここから見せる(3..6 は文字を書く) */
#define BAN_SUCK  96               /* ここから吸い込む */
#define BAN_END   (BAN_SUCK + 22)  /* 吸い込み: 帯の上端を 2 フレームに 1 行下げる(HUD の手前 6 行まで) */
#define BAN_STG   186              /* 短冊を使っているときの文字の置き場(ページ 2。帯には見えない起動ロゴの黒い区間) */
void banner_vram(void);            /* 第 2 バンク */
void juice_frame(void);            /* 撃破の演出(第 2 バンクの juice.c。毎フレーム) */
/* ---- 強化の ECHO / AEGIS / DROP(第 2 バンクの weapon.c。指示書 2.3) ---- */
void weap_init(void);              /* 出撃・復活: 分身の軌跡を自機の位置で埋める */
void weap_update(void);            /* player_update の終わり(分身・防壁・爆雷) */
void weap_draw(void);              /* スプライトの段の最後(player_weap_draw) */
void player_weap_draw(void);
void shots_draw(void);             /* 自機弾と爆雷(第 2 バンクの weapon.c。player_draw から) */
void lb_draw(void);                /* LIMIT BREAK の衝撃波(g_lb の間) */
extern u8 g_fired;                 /* このフレームに自機が撃った(分身も撃つ) */
extern u8 g_drops;                 /* 生きている DROP の爆雷の数 */
extern u8 g_shot_at;               /* pl_fire_from が次に空きを探す弾の枠 */
void pl_fire_from(s16 x, s16 y);   /* (x, y)(固定小数点、自機の左上)から今の主砲を撃つ(第 2 バンク) */
/* DROP の爆雷が這う地形: 面が今の区間の床・天井の表を置く(列の種類 g_map[c] → 世界の行)。lo = 0 なら地形なし */
typedef struct { const u8 *lo, *hi; u8 lo_add; u8 flat; u16 ncol; } Terrain;
extern Terrain g_ter;              /* flat: 0 以外なら表の代わりに平らな床(世界の行。1 面の街路) */
void gauge_steal(void);             /* 強化を 1 段奪われる(3・7 面の Scanner。第 2 バンク) */
void player_init(void);
void player_update(void);
void player_draw(void);
void player_hit(void);
void enemy_init(void);
void enemy_update(void);
void enemy_draw(void);
void limit_break(void);             /* ∞: 画面の敵と弾を一掃 */
enum { WAVES_NONE, WAVES_AIR, WAVES_CITY, WAVES_SKY };   /* SKY = 面の頭の空中戦(長く濃い。M7 の後) */
void enemy_waves(u8 set);           /* 出現の台本を区間に合わせて切り替える(時刻は 0 から) */
extern u8 g_wave_mirror;            /* 1 = 編隊を左から出す(3 面の逆走) */
void enemy_spawn_spirit(u16 wx, u8 roof);
void enemy_spawn_notch(s16 x, s16 y);   /* 1 機だけ、上へ(固定小数点) */
void enemy_bullet(s16 x, s16 y, s16 vx, s16 vy);   /* 固定小数点 */
void enemy_shoot(s16 x, s16 y);     /* 自機を狙う */

/* ---- ボスの状態(本体に置く。面のオーバレイが動かす) ---- */
enum { BS_NONE, BS_ENTER, BS_FIGHT, BS_DYING, BS_DONE };
extern u8  g_boss_state;
extern u8  g_boss_hp;
extern s16 g_boss_aim;              /* 今撃てる弱点の画面の y(-1 = なし)。検証の自機をそこへ寄せる */
extern u8  g_boss_dmg;              /* 受け取っていない傷(boss_damage が足す) */
extern u16 g_lg_on[26];             /* LEGION の積んであるマス(検証も読む) */
extern s16 g_lg_x0;
void boss_damage(u8 n);             /* LIMIT BREAK など: 次のフレームで面が受け取る */
extern u8 g_lb;                     /* LIMIT BREAK からの残りフレーム(0 = なし)。面の小物(Pawn など)はこの間に消える */
#define LB_NOW (g_lb > 18)          /* 面の砲台など: 画面の中のものはこの間に壊れる(処理落ちで飛ぶフレームがあっても届くよう数フレーム) */

/* ---- 重力(2 面。指示書 5.2)。自機・敵弾・アイテムに効く ----
   g_grav: GRAV_NONE / GRAV_DIR(一様: g_gx, g_gy。1/16 ドット/フレーム² 単位)/ GRAV_CENTER(画面の (g_gcx, g_gcy)
   へ、距離に比例: 1 ドット離れるごとに 1/512 ドット/フレーム²)。g_pl_ymin / g_pl_ymax: 自機の上下の限り(地形) */
enum { GRAV_NONE, GRAV_DIR, GRAV_CENTER };
extern u8  g_grav;
extern s16 g_gx, g_gy, g_gcx, g_gcy;
extern u8  g_pl_boost;              /* 上下の速さに足す(1/16 ドット/フレーム。3 面の高速の区間) */
extern u8  g_well_r;                /* GRAV_CENTER が引く範囲(ドット。ふつう 56) */
extern s16 g_pl_ymin, g_pl_ymax;    /* ドット(自機の左上の y) */
extern s16 g_pl_dy;
extern u8  g_demo;                  /* 1 = デモ(自動操縦。main.c) */                 /* 自機を描くときに y へ足す(帯の R#23 でスプライトがずれる場面の打ち消し。普段は 0) */
void mag_pair(u8 *p, u8 m);         /* R#1 の組(RG1SAV に MAG を足す。stage.c) */
extern u8  g_mag;                   /* 拡大スプライトの帯(1 = 上、2 = 下。stage.c)*/
extern u8  g_roll;                  /* 画面の縦のロール(R#23 に足す。スプライトは動かさない) */
/* ---- ∞ の紋章(infx.c。ROTOR のスプライト用) ---- */
void infx_start(s16 x, s16 y);      /* 確定の演出: (x, y) から画面の中央へ */
void infx_title(u8 on);             /* タイトルの紋章 */
void infx_reset(void);
void infx_update(void);             /* 論理の段 */
void infx_vram(void);               /* VRAM の窓 */
void infx_draw(void);               /* スプライトの段 */
extern u8 g_infx_mode;              /* 0 なら上の 3 つは呼ばない(第 2 バンク) */
/* ---- タイトルへ戻る理由とランキング(指示書 4.3 / 4.4)。タイトルのオーバレイが読む ---- */
enum { AO_BOOT, AO_OVER, AO_DEMO, AO_LOOP };   /* AO_LOOP: エンディングから 2 周目の出撃へ */
extern u8 g_demo_k, g_demo_save;    /* デモの持ち回りと、面セレクトの元の値(main.c) */
extern u8 g_reload;                 /* 1 = タイトルの素材を読み直して g_after_over の場面へ(main.c) */
extern u8 g_after_over;             /* AO_OVER: ゲームオーバー(ネームエントリー → 巻き戻し)、AO_DEMO: デモの終わり(ランキング) */
extern u16 g_last_score;            /* ゲームオーバーの得点(10 点単位) */
#define RANK_N 10
extern u8 g_rank_nm[RANK_N][3];
extern u16 g_rank_sc[RANK_N];       /* 10 点単位 */
extern u8 g_rank_ok;                /* 0x5A = 表を作った */
extern u8 g_title_mode;             /* タイトルの中の場面(選曲が見る。1 = ネームエントリー) */
extern u8 g_vdp_mon;                /* 隠しコンフィグの VDP MONITOR: 外枠の色で 1 フレームの時間の使い方を見せる */
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
u8   stage_col_ready(u16 wcol);           /* 世界の列 wcol が描き終わっている(上に BG を写してよい) */
extern u8 g_bands_all;
extern u8 g_blk_skip, g_blk_base;           /* リングのブロック base..base+skip-1 を面が使う(列・描き替えで描かない) */
RasEntry *stage_ev_raw(u8 line, u8 flags);  /* 並びを調べずに項目を足す(g_bands_all の面で、0 行の項目の後に 244 行など) */                      /* 1 = 面の ovl_bands がラスターの帯を全部出す(空 s < 0 の帯も) */
void stage_ev_vscroll(s16 line, u8 r23);    /* 帯の縦スクロール(R#23)。スプライトもずれるので自機の入れない帯で */
u8   stage_busy(void);                       /* 列の流し込み・描き替えが残っている(画面の BG に上書きする前に待つ) */
void stage_banner(const char *text);        /* 面の開始表示 */
void stage_banner_c(const char *text, u8 fg);   /* 文字の色つき */
extern u8 g_hall;                           /* 1 = 高い空を 7 面の測定空間(白い方眼)、2 = 8 面の縦穴(左右の壁)にする */
void stage_sky_redraw(void);                /* 高い空のブロックを全部描き直させる */
extern u8 g_start_stage;                    /* 隠しコンフィグの面セレクト(0 / 1 = 1 面から) */
extern volatile u8 g_dbg_invul;             /* 無敵(隠しコンフィグ・検証): 1 = 何にも当たらない、2 = 敵弾だけ当たらない(地形・ボス・体当たりは本来どおり) */
extern u8 g_calib;                          /* 7 面の試験(0 = 普通の面、1..6 = 過去の面のボスの再校正版、7 = LIBRA) */
extern u8 g_loop;                           /* 周回(0 = 1 周目) */
#define RECAL (g_calib || g_loop)           /* ボスの再校正版の動き(7 面の試験と、2 周目以降の 1〜6 面) */
void calib_begin(u8 k);                     /* 試験 k の始まり(各面の ovl_init から): 開始表示 */
u8   calib_out(u16 t);                      /* 試験に合格したあと(各面から毎フレーム): 判定文 → 上がって次の試験へ。1 = 頼んだ */
typedef struct { u8 s0, s1, t0, t1, strip[8]; } FarCfg;   /* 遠景(stage.c): 使う短冊 s0..s1・s の範囲 t0..t1-1・遠景の短冊 8 本 */
extern FarCfg *g_far;              /* 0 = 使わない。オーバレイの変数を指す */
u16 far_step(u16 a);               /* 第 2 バンク: 遠景の帯の横位置。a = bg の位置(14 ビット) | 指示 << 14(FAR_LOCK / FAR_FREE / FAR_RESET) */
#define FAR_LOCK  0x0000u          /* bg と一緒に動きつつ、ずれを 1/8 ずつ 0 へ */
#define FAR_FREE  0x4000u          /* 半分の速さ */
#define FAR_RESET 0x8000u          /* 置き直す(ずれ 0) */
#define FAR_CITY  0xC000u          /* 1 面: 流す・戻すを第 2 バンクが決める(都市の前半・後半だけ流し、広場の手前で戻す。g_far の t1 = 0 なら設定を書く) */
void stage_ev_scroll(s16 line, u16 x);      /* ラスターリスト(ovl_bands の中で、行の順に) */
void stage_ev_pal(s16 line, const u8 *rgb);
void stage_ev_ps(s16 line, const u8 *rgb, u16 x);   /* 色 1 と横スクロールを 1 つの項目に */
void stage_ev_pal_i(s16 line, u8 idx, const u8 *rgb);   /* 色 idx を(1 項目に 1 色)*/
void stage_ev_haze(s16 line, u8 n, u8 *tbl);  /* HR ポーリング帯(R#27 を毎ライン。tbl は (r26, r27) × n) */
void ring_cmd(u16 sx, u16 sy, s16 x, s16 y, u8 w, u8 h, u8 fill, u8 byte);   /* 画面の矩形 → 表示リング(GHOST) */

#endif /* GAME_H */
