/* main.c — Criterion 本体(M1: 1 面の空中戦が 60Hz スクロールで遊べるところまで)。
   起動 → 背景・HUD・スプライトの初期化(BIOS の CHGMOD を使うのはここまで)→ RAM 実行・直接割込み →
   毎フレーム: 入力 → 自機 → 敵 → スクロール → スプライト → ラスターリスト → HUD。
   ★ステージ開始表示・タイトル・出撃シーンは M2(指示書 9)。今は起動してすぐ空中戦が始まる。 */
#include "types.h"
#include "vdp.h"
#include "sys.h"
#include "frame.h"
#include "raster.h"
#include "spr.h"
#include "input.h"
#include "font.h"
#include "game.h"
#include "bank.h"
#include "rom.h"
#include "assets.h"
#include "ghost.h"
#include "ovl.h"
#include "snd.h"
#include "snd_data.h"
#define SPRITE_SCHEMES
#include "sprite_data.h"

enum { ST_PLAY, ST_OVER };
extern u8 g_snd_room;

/* 計測・検証用(デバッガから読む / 書く) */
typedef struct {
    u16 base;          /* 何もしないフレームのアイドル回数 */
    u16 idle_min;
    u16 late;          /* 遅れたフレーム */
    u16 frames;
    u32 idle_sum;
    u8  state;
    u8  dropped;       /* スプライトが表に入りきらなかった累計(255 で止める) */
    u8  phase;         /* 今の場面(PH_*) */
    u16 slow;          /* 処理落ちさせたフレーム(敵の更新を飛ばした) */
} GameStat;
volatile GameStat g_stat;
volatile u8 g_dbg_invul;
s16 g_boss_aim = -1;                /* 今撃てる弱点の y(検証が自機を寄せる。-1 = なし。ボスが毎フレーム書く) */
/* 場面(タイトルのオーバレイが動かすが、本体も見るので本体に置く) */
u8  g_phase;
u16 g_pt;
u8  g_split_n;
u8  g_split_shown;
u8  g_hud_on;
/* 処理落ち(指示書 8.3「重い場面では 30fps に落としても構わない。スクロールが滑らかなまま敵だけ処理落ち」):
   前のフレームが遅れたか、余りが少なかったら、このフレームは敵・ボス・Pawn の更新とスプライトの描き直しを
   飛ばす(続けては飛ばさない = ゲームの進みは最低 30fps)。スクロール・ラスター・自機の操作は毎フレーム */
u8 g_slow;
static u8 slow_next;
u16 g_frame_t0;                    /* このフレームの始まり(S1990 タイマ。stage.c も残り時間に使う) */
#define HUD_DEADLINE (170u * 16u)  /* これを過ぎていたら HUD の文字の書き込みを次のフレームへ */
#define COL_DEADLINE (150u * 16u)  /* これを過ぎていたらスプライトの色表の書き込みを次のフレームへ */
#define SPR_DEADLINE (100u * 16u)  /* スプライトの段に入る時点でこれを過ぎていたら、そのフレームは描き直さない
                                      (スプライト・属性表・VRAM の仕事で残り 140 ライン前後かかる) */      /* 1 = 被弾しない(自動テスト用) */

static u8 state;

/* ---- デモ(M7 の後、指摘 B8): タイトルで 15 秒触らないと、自動操縦の 1 面でパワーアップの使い方を見せる。
   何か押すとタイトルへ戻る。説明は面の開始表示の帯に出す(フォントは英大文字・数字と - . " , / だけ) ---- */
u8 g_demo;
u8 g_demo_k, g_demo_save;                                   /* デモの持ち回り(1・3・4・5 面。指示書 4.2)と、面セレクトの元の値 */
static u16 demo_t;
static u8 demo_step, demo_seen, demo_prev;
static void demo_input(void) {
    u8 v = IN_A, i;
    s16 px = (s16)(PX(g_pl.x) + 8), py = (s16)(PX(g_pl.y) + 8), tx = 56, ty = 90, bd = 0x7FFF;
    demo_t++;
    for (i = 0; i < MAX_ITEM; i++)                             /* アイテムがあれば取りに行く */
        if (g_item[i].on) { tx = PX(g_item[i].x); ty = PX(g_item[i].y); bd = 0; break; }
    if (bd) for (i = 0; i < MAX_ENEMY; i++) {                  /* 無ければ前にいちばん近い敵の高さへ */
        s16 ex;
        if (!g_enemy[i].type) continue;
        ex = PX(g_enemy[i].x);
        if (ex > px && (s16)(ex - px) < bd) { bd = (s16)(ex - px); ty = PX(g_enemy[i].y); }
    }
    if (py < ty - 3) v |= IN_DOWN; else if (py > ty + 3) v |= IN_UP;
    if (px < tx - 3) v |= IN_RIGHT; else if (px > tx + 3) v |= IN_LEFT;
    if (!(demo_seen & 4)) { if (g_phase >= PH_AIR) { demo_seen |= 4; stage_banner("DEMONSTRATION"); } }
    else if (!(demo_seen & 1) && !bd && demo_t > 200) { demo_seen |= 1; stage_banner("FORMATION DOWN - QUANTA"); }
    if (!(demo_seen & 2) && g_gauge.cursor) { demo_seen |= 2; stage_banner("QUANTA MOVES THE CURSOR"); }
    /* THRUST(1)→ ECHO(2)で確定、最後は中心(4)で LIMIT BREAK */
    if (g_gauge.armed && !(demo_prev & IN_B) && demo_step < 3 && g_gauge.cursor == (u8)(demo_step == 2 ? 4 : demo_step + 1)) {
        v |= IN_B;
        stage_banner(++demo_step == 3 ? "CENTER / B - LIMIT BREAK" : "PRESS B - POWER UP");
    }
    g_in_edge = (u8)(v & ~demo_prev);
    g_in = demo_prev = v;
}

/* ---- 撃破の演出(M7 の後、指摘 C4): 中ボス = ちょっとした破壊、面ボス = 派手、ラスボス = やり過ぎなくらい。
   ボスが BS_DYING になった区間で段階を決め、画面の揺れ(R#23 に足す g_roll)・閃光(色 0)・爆発の追加・爆発音を出す。
   ★区間の番号は各面の sN.h の enum と合わせる(本体は面のヘッダを取り込まないので数で持つ) ---- */
static const u8 boss_sec[] = { 5, 13, 22, 31, 41, 50, 53, 60, 61 };   /* 1〜6 面のボス、7 面 LIBRA、8 面の第 1・第 2 形態 */
#define SEC_LAST 62                                                   /* 8 面の第 3 形態(SEC8_BOSS3) */
/* ★部品の多いボスの区間: 戦っている間はスプライトの枠をボスの割り振りに(spr_layout。1 つの帯に 1 枚の敵が 7〜10 枚並び、
   ふつうの 6 枠では消えて見えた)。1 面 PLUMB の鎖、2 面 CW、3 面 Metronome、4 面 VERNIER の光線、6 面の砂時計の目 */
static const u8 big_sec[] = { 5, 11, 20, 29, 48, 31, 41, 60, 62, 3 };
static const u8 big_lay[] = { 2, 2, 7, 1, 3, 4, 4, 4, 5, 4 };       /* 割り振り(spr.c の layouts) */
/* ボスがいなくても区間で替える: 3 面の最高速(隔壁の柱が 1 枚の敵の枠で、ふつうの 6 枠では柱が消えて見えない壁になった)、
   8 面の脱出と宇宙へ抜けるまで(降る破片が敵弾の枠) */
static const u8 sec_sec[] = { 21, 63, 64, 0, 7, 15, 24, 33, 44, 52, 55 };
static const u8 sec_lay[] = { 1, 4, 4, 6, 6, 6, 6, 6, 6, 6, 6 };    /* 0.. = 各面の空中戦(編隊の組を多く) */
u8 g_infx_mode;                    /* ∞ の表示(第 2 バンクの infx.c)。0 = なし */
u8 g_after_over, g_title_mode, g_rank_ok, g_vdp_mon, g_reload;
#define MON(c) do { if (g_vdp_mon) vdp_wreg(7, (c)); } while (0)   /* VDP MONITOR: 外枠 = 論理 12・スプライト 9・VRAM 13・待ち 0(割込みの間は 15) */
u16 g_last_score;
u8 g_rank_nm[RANK_N][3];
u16 g_rank_sc[RANK_N];
static u8 juice_t, juice_tier, juice_len, bs_last;
static void juice(void) {
    u8 s = g_boss_state;
    if (s == BS_DYING && bs_last != BS_DYING) {
        u8 i;
        juice_tier = 1;
        if (!g_calib) {
            if (g_sec == SEC_LAST) juice_tier = 3;
            else for (i = 0; i < sizeof boss_sec; i++) if (boss_sec[i] == g_sec) juice_tier = 2;
        }
        juice_len = juice_t = juice_tier == 1 ? 45 : juice_tier == 2 ? 120 : 255;
        if (juice_tier == 2 || g_calib)                       /* 面のボス・試験のボス: Infinity Shard(指示書 2.3「ボス撃破時のみ出現」) */
            item_spawn(FX(160), FX(g_boss_aim > 16 && g_boss_aim < PLAY_H - 16 ? g_boss_aim : 80), 1);
    }
    bs_last = s;
    g_mag = 0;
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

void game_start(void) {
    player_init();
    enemy_init();
    g_gauge.cursor = N_INF; g_gauge.armed = 0; g_gauge.anim = 0; g_gauge.flash = 0;
    gauge_lose();
    g_vscroll = 0;
    g_boss_state = BS_NONE; g_boss_dmg = 0; g_calib = 0; g_loop = 0; g_hall = 0;   /* 新しいゲームは 1 周目から */
    infx_reset();
    state = ST_PLAY;
}

static void screen_init(void) {
    __asm
        ld   a, #5
        ld   (0xFCAF), a
        call 0x005F          ; CHGMOD
    __endasm;
    vdp_wreg(8, 0x28);       /* TP, VR、スプライト表示 */
    vdp_wreg(9, 0x80);       /* 212 ライン、NTSC */
    vdp_wreg(2, 0x3F);       /* SP2: ページ 0|1 */
    vdp_wreg(25, 0x03);      /* SP2 | MSK */
    vdp_wreg(7, 0);
}

/* 上の帯が SCREEN12 の間は、VRAM を下の区間でしか書けない(M0-4)。境界の項目(RAS_MARK)を待つ。
   待ったらフレーム待ちの基準も置き直す(ループの位相が境界に揃うため)。
   ★見るのは表示中のリスト(前のフレームに作ったもの)の分割 g_split_shown だけ。g_split_n が 0 になったフレームも
     表示中のリストはまだ 244 行で SCREEN12 に切り替わる(待たずに書いたら HUD の文字がプレイエリアへ化けた)。
     逆に g_split_n を立てたフレームの表示中のリストには境界の項目が無いので、待つと 370 ライン空回りした(6 面)。 */
static void wait_vram_window(void) {
    u8 m0, i;
    if (!g_split_shown) return;
    m0 = g_ras_mark;
    for (i = 0; i < 200 && g_ras_mark == m0; i++) {
        u16 t0 = sys_timer();
        while (g_ras_mark == m0 && (u16)(sys_timer() - t0) < 30) { if (g_poll_hook) g_poll_hook(); }   /* ★待つ間も PCM を出す(M7) */
    }
    frame_begin();
}

/* ページ 0 のコード(CODE0、0x0100〜)の長さ = リンカが決める l__CODE0 */
static u16 code0_len(void) __naked {
    __asm
        ld   de, #l__CODE0
        ret
    __endasm;
}

/* ページ 0 のコードを ROM の bank4-5 から RAM へ。★直接割込み方式にしてから(ページ 0 が RAM) */
static void code0_load(void) {
    rom_to_ram(4, 0x0100, (u8 *)0x0100, code0_len());
}

void main(void) {
    sys_r800();
    screen_init();
    *(volatile u8 *)0xF3E0 &= 0xBF;                         /* 読み込みの間は表示を止める(BL = 0)。★RG1SAV ごと変える */
    vdp_wreg(1, *(volatile u8 *)0xF3E0);                    /*   (spr_init が RG1SAV から R#1 を書き直すので) */
    vdp_hmmv(0, 0, 256, 1024, 0x00);
    vdp_cmd_wait();
    bg_init();
    spr_init();
    sys_ram_exec();
    g_opll = 1;                      /* MSX-MUSIC は turboR に内蔵 */
    ras_init();
    ras_set_isr(RAS_ISR_DIRECT);
    code0_load();                    /* ここから先でページ 0 のコード(ROTOR / GHOST / 選曲と効果音)を呼べる */
    bk_load();                       /* ここから先で第 2 バンク(bank.h)を呼べる */
    rom_to_vram(ASSET_SPR_PAT_BANK, ASSET_SPR_PAT_OFF, 1, 0xF800, ASSET_SPR_PAT_LEN);   /* スプライトの絵(64 パターン。63 番 = 空白)。
                                                                         ★ROM に置く(常駐に置くと 1.3KB。M7 の後) */
    snd_init();
    ovl_load_sync(OVL_TITLE);        /* タイトルのオーバレイ */
    title_load();                    /* ROM の素材(ページ 1-2 を一時的にカートリッジへ戻して読む) */
    game_start();
    title_start();

    frame_begin();
    { u8 j; u16 m = 0; frame_sync(1); for (j = 0; j < 8; j++) { u16 v = frame_sync(1); if (v > m) m = v; } g_stat.base = m; }
    g_stat.idle_min = 0xFFFF;

    for (;;) {
        static u8 demo_end;
        static u8 col_now, col_done, pat_now;
        static u16 over_t;
        static s8 over_spd;      /* ★初期値つきの static は常駐の空きを食うので使わない(起動直後の 0 は「色の組 0 を 4 つ写す」) */
        u16 late0 = g_frame_late, idle;
        u8 ph;
        MON(0);
        idle = frame_sync(1);
        g_frame_t0 = sys_timer();
        MON(12);
        if (g_frame_late != late0) g_stat.late++;
        g_slow = slow_next;
        slow_next = (u8)(!g_slow && g_phase >= PH_AIR && (g_frame_late != late0 || idle < (g_stat.base >> 2)));
        if (g_slow) g_stat.slow++;
        if (idle < g_stat.idle_min) g_stat.idle_min = idle;
        g_snd_room = (u8)(idle >= (g_stat.base >> 2) + (g_stat.base >> 4));   /* 空き時間が平常の 3 割以上(PCM は約 2 割を使う) */
        g_stat.frames++;
        g_stat.idle_sum += idle;

        /* 音: 選曲と演出 → ドライバ。★毎フレーム必ず回す(先送りすると音が固まって出て、リズムがもたついた。M7 の測定で
           16 分音符の半分が 1 フレームより大きくずれた) */
        juice();
        if (g_lb) g_lb--;
        {
            u8 s = g_boss_state, l = 0, i;
            if (s != BS_NONE && s != BS_DONE) for (i = 0; i < sizeof big_sec; i++) if (big_sec[i] == g_sec) l = big_lay[i];
            for (i = 0; i < sizeof sec_sec; i++) if (sec_sec[i] == g_sec) l = sec_lay[i];
            if (g_gauge.echo || g_gauge.aegis) l += 8;         /* 強化の分身・防壁: G_OPT の 4 枠がある割り振り */
            if (l != g_spr_layout) spr_layout(l);
        }
        music_frame(state == ST_OVER); pcmx_service(); snd_frame(); pcmx_service();   /* ★間にも PCM の送出口(合わせて約 0.7ms。M7) */
        pcm_keep();
        if (g_phase >= PH_AIR) stage_pump_nb();   /* 列の描き込みと GHOST: VDP が空いていれば出す(待たない)。1 フレームに数か所 */
        input_read();
        if (g_demo) {                                          /* デモ: 何か押すか 50 秒でタイトルへ */
            if (g_in || demo_t >= 3000) { g_demo = 0; demo_end = 1; g_after_over = AO_DEMO; g_start_stage = g_demo_save; }   /* デモの後はランキング */
            else demo_input();
        }
        ph = g_phase;
        if (ph <= PH_SORTIE) title_update();
        if (state == ST_OVER) g_scroll_speed = over_spd;          /* ゲームオーバー: 面の進行に関わらず慣性で止まる */
        bg_scroll();
        if (g_phase >= PH_AIR) stage_update();   /* 敵(砲台)が世界の位置を使うので、敵より先に */
        if (g_infx_mode) infx_update();                       /* ★第 2 バンクは仕事のあるときだけ呼ぶ(毎フレームの呼び出しで重い場面が遅れた) */
        if (ph >= PH_SORTIE && state == ST_PLAY) {
            if (g_dbg_invul || g_demo) g_pl.invul = 2;
            player_update();
            if (ph >= PH_AIR) { stage_pump_nb(); if (!g_slow) { enemy_update(); if (g_ovl_ready && g_ovl >= OVL_S1) ovl_logic(); } }
            if (!g_pl.alive && !g_pl.dead_t && !g_pl.lives) { state = ST_OVER; over_t = 0; over_spd = g_scroll_speed; }
        } else if (state == ST_OVER) {
            /* ゲームオーバー(指示書 4.3): スクロールが慣性で止まり、文字を 1 字ずつ押す。押し終えたら A か 5 秒でネームエントリーへ */
            over_t++;
            if (!(over_t & 3)) over_spd = over_spd > 0 ? (s8)(over_spd - 1) : over_spd < 0 ? (s8)(over_spd + 1) : 0;
            if (over_t >= 30 && !(over_t & 7) && over_t < 30 + 8 * 9) {
                static char ov[10];
                u8 k = (u8)((over_t - 30) >> 3), i;
                for (i = 0; i < 9; i++) ov[i] = i <= k ? "GAME OVER"[i] : ' ';
                ov[9] = 0;
                font_text(HUD_PAGE, 136, HUD_ROW1, 13, 0, ov);
                if (ov[k] != ' ') sfx(SFX_TICK);
            }
            if ((over_t > 110 && (g_in_edge & IN_A)) || over_t > 400) { demo_end = 1; g_after_over = AO_OVER; g_last_score = g_pl.score; }
        }
        if (g_reload) { g_reload = 0; demo_end = 1; }
        if (demo_end) {
            /* ゲームオーバー・デモの終わり: タイトルの素材を読み直して(読む間は表示を止める)、タイトルのオーバレイの場面へ
               (g_after_over: ゲームオーバー → ネームエントリー → 巻き戻し → タイトル、デモ → ランキング → タイトル) */
            *(volatile u8 *)0xF3E0 &= 0xBF;
            vdp_wreg(1, *(volatile u8 *)0xF3E0);
            bg_init();                   /* 表示リングは都市や空に描き替わっている */
            ovl_load_sync(OVL_TITLE);
            title_load();
            if (g_after_over != AO_LOOP) game_start();       /* 2 周目は得点・残機・強化を引き継ぐ */
            else state = ST_PLAY;
            title_start();
            frame_begin();
            demo_end = 0; demo_t = 0; demo_step = 0; demo_seen = 0; demo_prev = 0;
        }

        /* ★処理落ちのフレームはスプライトも描き直さない(前のフレームの属性表のまま。敵は動いていないので
           止まって見えるのは同じ。スプライトの段は 1 フレームで 70 ライン前後かかる) */
        if (!g_slow && g_phase >= PH_AIR && (u16)(sys_timer() - g_frame_t0) > SPR_DEADLINE) {
            g_slow = 1; g_stat.slow++;   /* ここまでで重かった: スプライトは前のフレームのまま */
        }
        MON(9);
        if (!g_slow) {
            spr_voff = (u8)(g_vscroll + g_roll);   /* スプライトの Y はプレイエリアの R#23 を足して表へ(ロールは画面だけ) */
            spr_begin();
            if (g_phase <= PH_TITLE) title_draw();
            else { if (g_phase == PH_SORTIE) title_draw(); player_draw(); if (g_infx_mode) infx_draw(); if (g_ovl_ready && g_ovl >= OVL_S1) ovl_draw(); enemy_draw(); hud_sprites(); }   /* ∞ は G_MISC の 4 枠を先に取る */
            if (g_phase <= PH_TITLE && g_infx_mode) infx_draw();
        }
        wait_vram_window();              /* ここから先は VRAM を書く */
        MON(13);
        /* ★CPU で VRAM を書く仕事(属性表・HUD の文字)を先に、VDP のコマンドを出す仕事(stage_vram)を後に。
           VDP がコマンドを実行している間は CPU の VRAM の書き込みも遅くなる(Pawn を足したら 1 フレームが
           90 ライン延びた)。コマンドはフレームの頭とここで出し、次のフレームの論理の間に VDP が進める */
        if (!g_slow) spr_end();
        if (g_infx_mode) infx_vram();
        /* スプライトの色の組: オーバレイ(面)が替わったら、帯の色表 4 つ(各 448 バイト。G_MISC の 4 枠は使うときに書くので
           写さない)を 112 バイトずつ 16 フレームで写す(1 回 ≒ 1ms)。★1 つを 1 フレームで写すと(4ms)、7 面の試験で
           オーバレイを読み直すたびに遅れた。面ごとに体の色を背景と反対の色相にする(M7 の後) */
        {   /* ★7 面の試験(過去の面のオーバレイを読み直す)の間は 7 面の色の組のまま(試験ごとに写すと PCM と重なって遅れた) */
            u8 want = (u8)(spr_scheme[g_calib ? OVL_S7 : g_ovl] + g_spr_layout * 3);   /* 割り振りごとに 3 つ(gen_sprites.py) */
            if (g_ovl_ready && want != col_now) { col_now = want; col_done = 0; }
        }
        if (g_ovl_ready && g_ovl != pat_now) {                 /* 面ごとの絵の組(gen_sprites.py の STAGE_GROUPS。約 1ms) */
            pat_now = g_ovl;
            rom_to_vram(ASSET_SPR_SET_BANK, (u16)(ASSET_SPR_SET_OFF + (u16)pat_now * SPR_SET_LEN), 1, (u16)(0xF800u + SPR_SET_BASE * 32u), SPR_SET_LEN);
        }
        if (col_done < 16 && (u16)(sys_timer() - g_frame_t0) < COL_DEADLINE) {
            u16 o = (u16)(col_done & 3) * 112u;
            rom_to_vram(ASSET_SPR_COL_BANK, (u16)(ASSET_SPR_COL_OFF + ((u16)col_now << 9) + o), 1,
                        (u16)(((const u16 *)sat_addr)[col_done >> 2] - 0x200u + o), 112);
            col_done++;
        }
        /* HUD の文字は次のフレームへ回してよい: フレームの残りが少なければ書かない(VDP のコマンドと重なると
           2 行の書き込みが 50 ライン近くかかることがある) */
        if (g_hud_on && (g_phase < PH_AIR || (u16)(sys_timer() - g_frame_t0) < HUD_DEADLINE)) hud_update();
        if (g_phase <= PH_SORTIE) { title_vram(); title_build(); }
        else { stage_vram(); stage_build(); }
        if (g_phase == PH_LOGO && g_pt == 2) {                 /* 起動ロゴのラスターリストが効いてから表示を戻す */
            *(volatile u8 *)0xF3E0 |= 0x40;
            vdp_wreg(1, *(volatile u8 *)0xF3E0);
        }

        g_pt++;
        g_stat.state = state;
        g_stat.phase = g_phase;
        if (g_spr_dropped && g_stat.dropped < 255) { g_stat.dropped++; g_spr_dropped = 0; }
    }
}
