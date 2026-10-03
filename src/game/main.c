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
#include "rom.h"
#include "ghost.h"
#include "ovl.h"
#include "snd.h"
#include "snd_data.h"
#define SPRITE_PATTERNS                 /* 絵の表はここだけに置く(sprite_data.h) */
#include "sprite_data.h"

enum { ST_PLAY, ST_OVER };
static u8 snd_skip;
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
#define SPR_DEADLINE (100u * 16u)  /* スプライトの段に入る時点でこれを過ぎていたら、そのフレームは描き直さない
                                      (スプライト・属性表・VRAM の仕事で残り 140 ライン前後かかる) */      /* 1 = 被弾しない(自動テスト用) */
/* 段ごとの所要時間(S1990 タイマのカウント。1 ライン ≒ 16.27): 入力・自機・敵・スプライト・ラスター・HUD */
#define NPROF 6
volatile u16 g_prof_max[NPROF];
volatile u32 g_prof_sum[NPROF];
static u16 prof_t;
static void prof(u8 k) {
    u16 now = sys_timer(), d = (u16)(now - prof_t);
    prof_t = now;
    if (d > g_prof_max[k]) g_prof_max[k] = d;
    g_prof_sum[k] += d;
}

static u8 state;

void game_start(void) {
    player_init();
    enemy_init();
    g_gauge.cursor = N_INF; g_gauge.armed = 0; g_gauge.anim = 0; g_gauge.flash = 0;
    gauge_lose();
    g_vscroll = 0;
    g_boss_state = BS_NONE; g_boss_dmg = 0; g_calib = 0; g_hall = 0;
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
        while (g_ras_mark == m0 && (u16)(sys_timer() - t0) < 30) { }
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
    spr_init(spr_pat, PAT_COUNT);
    sys_ram_exec();
    snd_detect();                    /* ★BIOS(RDSLT)を使うので直接割込み方式の前に */
    ras_init();
    ras_set_isr(RAS_ISR_DIRECT);
    code0_load();                    /* ここから先でページ 0 のコード(ROTOR / GHOST / 選曲と効果音)を呼べる */
    snd_init();
    ovl_load_sync(OVL_TITLE);        /* タイトルのオーバレイ */
    title_load();                    /* ROM の素材(ページ 1-2 を一時的にカートリッジへ戻して読む) */
    game_start();
    title_start();

    frame_begin();
    { u8 j; u16 m = 0; frame_sync(1); for (j = 0; j < 8; j++) { u16 v = frame_sync(1); if (v > m) m = v; } g_stat.base = m; }
    g_stat.idle_min = 0xFFFF;

    for (;;) {
        u16 late0 = g_frame_late, idle;
        u8 ph;
        idle = frame_sync(1);
        g_frame_t0 = sys_timer();
        if (g_pcm_cur != SND_NONE) g_frame_t0 -= 64u * 16u;   /* ★PCM の刻みが CPU の約 4 割を使うので、締め切り(g_frame_t0 からの
                                                                ライン数)を 64 ライン前倒しにする */
        if (g_frame_late != late0) g_stat.late++;
        g_slow = slow_next;
        slow_next = (u8)(!g_slow && g_phase >= PH_AIR && (g_frame_late != late0 || idle < (g_stat.base >> 2)));
        /* PCM(刻み方式)は CPU の約 4 割を使う: 声(出撃・警告・試験の入りなど敵の少ない場面)の間は論理を止め、
           ほかの PCM(余裕のあるときだけ鳴る)の間は 1 フレームおきに */
        if (g_pcm_cur != SND_NONE && (g_pcm_cur >= PCM_V01 || !g_slow)) slow_next = 1;
        if (g_slow) g_stat.slow++;
        if (idle < g_stat.idle_min) g_stat.idle_min = idle;
        g_snd_room = (u8)(idle >= (g_stat.base >> 1));
        g_stat.frames++;
        g_stat.idle_sum += idle;

        prof_t = sys_timer();
        /* 音: 選曲と演出 → ドライバ。★前のフレームに空き時間が無ければ 5 フレームまで先送り(テンポは JIFFY で
           数えているので次でまとめて進む)。PCM の補充だけは毎フレーム */
        if (g_snd_room || ++snd_skip > 5) { snd_skip = 0; music_frame(state == ST_OVER); snd_frame(); }
        pcm_keep();
        if (g_phase >= PH_AIR) stage_pump_nb();   /* 列の描き込みと GHOST: VDP が空いていれば出す(待たない)。1 フレームに数か所 */
        input_read();
        prof(0);
        ph = g_phase;
        if (ph <= PH_SORTIE) title_update();
        bg_scroll();
        if (g_phase >= PH_AIR) stage_update();   /* 敵(砲台)が世界の位置を使うので、敵より先に */
        infx_update();
        if (ph >= PH_SORTIE && state == ST_PLAY) {
            if (g_dbg_invul) g_pl.invul = 2;
            player_update();
            prof(1);
            if (ph >= PH_AIR) { stage_pump_nb(); if (!g_slow) { enemy_update(); if (g_ovl_ready && g_ovl >= OVL_S1) ovl_logic(); } }
            prof(2);
            if (!g_pl.alive && !g_pl.dead_t && !g_pl.lives) {
                state = ST_OVER;
                font_text(HUD_PAGE, 136, HUD_ROW1, 8, 0, "GAME OVER");
            }
        } else if (state == ST_OVER && (g_in_edge & IN_A)) {
            /* ★ネームエントリーと巻き戻しは M8。今はタイトルの素材を読み直して起動ロゴから(読む間は表示を止める) */
            *(volatile u8 *)0xF3E0 &= 0xBF;
            vdp_wreg(1, *(volatile u8 *)0xF3E0);
            bg_init();                   /* 表示リングは都市や空に描き替わっている */
            ovl_load_sync(OVL_TITLE);
            title_load();
            game_start();
            title_start();
            frame_begin();
        }

        /* ★処理落ちのフレームはスプライトも描き直さない(前のフレームの属性表のまま。敵は動いていないので
           止まって見えるのは同じ。スプライトの段は 1 フレームで 70 ライン前後かかる) */
        if (!g_slow && g_phase >= PH_AIR && (u16)(sys_timer() - g_frame_t0) > SPR_DEADLINE) {
            g_slow = 1; g_stat.slow++;   /* ここまでで重かった: スプライトは前のフレームのまま */
        }
        if (!g_slow) {
            spr_voff = (u8)(g_vscroll + g_roll);   /* スプライトの Y はプレイエリアの R#23 を足して表へ(ロールは画面だけ) */
            spr_begin();
            if (g_phase <= PH_TITLE) title_draw();
            else { player_draw(); infx_draw(); if (g_ovl_ready && g_ovl >= OVL_S1) ovl_draw(); enemy_draw(); hud_sprites(); }   /* ∞ は G_MISC の 4 枠を先に取る */
            if (g_phase <= PH_TITLE) infx_draw();
        }
        wait_vram_window();              /* ここから先は VRAM を書く */
        /* ★CPU で VRAM を書く仕事(属性表・HUD の文字)を先に、VDP のコマンドを出す仕事(stage_vram)を後に。
           VDP がコマンドを実行している間は CPU の VRAM の書き込みも遅くなる(Pawn を足したら 1 フレームが
           90 ライン延びた)。コマンドはフレームの頭とここで出し、次のフレームの論理の間に VDP が進める */
        if (!g_slow) spr_end();
        infx_vram();
        /* HUD の文字は次のフレームへ回してよい: フレームの残りが少なければ書かない(VDP のコマンドと重なると
           2 行の書き込みが 50 ライン近くかかることがある) */
        if (g_hud_on && (g_phase < PH_AIR || (u16)(sys_timer() - g_frame_t0) < HUD_DEADLINE)) hud_update();
        prof(3);
        if (g_phase <= PH_SORTIE) { title_vram(); title_build(); }
        else { stage_vram(); stage_build(); }
        prof(4);
        if (g_phase == PH_LOGO && g_pt == 2) {                 /* 起動ロゴのラスターリストが効いてから表示を戻す */
            *(volatile u8 *)0xF3E0 |= 0x40;
            vdp_wreg(1, *(volatile u8 *)0xF3E0);
        }
        prof(5);

        g_pt++;
        g_stat.state = state;
        g_stat.phase = g_phase;
        if (g_spr_dropped && g_stat.dropped < 255) { g_stat.dropped++; g_spr_dropped = 0; }
    }
}
