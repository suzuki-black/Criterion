/* music_sel.c — 場面に合わせて曲を選ぶ(指示書 6.3)、場面の音の演出(第 4 バンク。2026-10-28 に第 2 バンクから移した。本体のループから毎フレーム)。
   区間(g_sec)は全面の通し番号なので、区間 → 曲の表を 1 つ持てば各面のオーバレイは曲を知らなくてよい。
   空中戦(M03)は面ごとに移調し、面の本体に入ると次の小節の頭で面の曲へ切り替える。ボスは警告(J02)から。 */
#include "game.h"
#include "snd.h"
#include "snd_data.h"

extern s8 snd_trans_next;                         /* snd.c */
extern u8 ch[];                                    /* snd.c の Ch(14 バイト。+6 = 音、+10 = 印) */
extern u8 vib_t;
extern s8 vib_fm, vib_psg;
extern const u16 fm_fnum[12], psg_per1[12];
void vib_apply(void);
u8 g_vib_delay, g_vib_depth;                       /* ★第 4 バンクの変数は初期値が付かない(sel_ok の初期化で入れる) */
static u8 vib_ph;
#define A_  0xF0   /* 空中戦(M03、面ごとに移調) */
#define S_  0xF1   /* 面の曲 */
#define MI  0xF2   /* 中ボス(M12) */
#define BO  0xF3   /* ボス(J02 → M13) */
#define OU  0xF4   /* クリア(J01) */
#define CO  0xF5   /* 5 面の撃沈: 爆発の間は止め、低いオケヒの後に哀歌(M08C) */
#define NSEC 69
static const u8 sec_song[NSEC] = {
    A_, S_, S_, MI, S_, BO, OU,                              /* 1 面  0.. 6 */
    A_, S_, S_, SONG_M05R, MI, S_, BO, OU,                   /* 2 面  7..14(10 = 重力反転: 曲も逆に) */
    A_, S_, S_, S_, SONG_M06R, MI, S_, BO, OU,               /* 3 面 15..23(19 = 逆走: 曲も逆に) */
    A_, S_, S_, S_, S_, MI, S_, BO, OU,                      /* 4 面 24..32 */
    A_, S_, S_, S_, S_, MI, S_, S_, BO, CO, OU,              /* 5 面 33..43(38 = 中ボス HANGAR、42 = 撃沈 → 哀歌) */
    A_, S_, S_, S_, MI, S_, BO, OU,                          /* 6 面 44..51 */
    S_, BO, OU,                                              /* 7 面 52..54 */
    A_, S_, S_, MI, S_, SONG_M14A, SONG_M14B, SONG_M14C, SONG_M15, SONG_M15,   /* 8 面 55..64 */
    SONG_M16, SONG_M16, SONG_M16, SONG_M16,                  /* 終幕 65..68 */
};
static const u8 sec_first[9] = { 7, 15, 24, 33, 44, 52, 55, 65, 255 };   /* 次の面の最初の区間 */
static const u8 stage_song[8] = { SONG_M04, SONG_M05, SONG_M06, SONG_M07, SONG_M08, SONG_M09, SONG_M10, SONG_M11 };
static const s8 air_trans[8] = { 0, 2, 3, 4, 5, 6, 7, 8 };     /* 空中戦は面ごとに上がっていく */

static u8  want, sec_prev, ph_prev, bs_prev, calib_prev, sel_ok;   /* ★バンクの変数は初期化されないので、最初の呼び出しで代入(sel_ok) */
static u16 mt;                                   /* 区間に入ってからのフレーム */
static u8  v04;                                  /* 8 面の声を鳴らした */
static u8  motif_t;                              /* 7 面の試験の頭: その面の曲を挟む残り */

/* 区間の中の音の演出(毎フレーム、値が変わったときだけドライバが書き直す) */
static void effects(u8 sec) {
    s8 bend = 0, det = 0;
    u8 tempo = 128, atten = 0;
    u16 mute = 0;
    if (sec == 10) {                                             /* 2 面 重力反転: 半音ずつ沈んで戻る */
        if (mt < 40) bend = (s8)-(s8)(mt >> 3);
        else if (mt < 80) bend = (s8)-(s8)((80 - mt) >> 3);
    } else if (sec >= 15 && sec <= 23) {                         /* 3 面: テンポをスクロールの速さに */
        s8 v = g_scroll_speed < 0 ? (s8)-g_scroll_speed : g_scroll_speed;
        if (v > 4) { v = (s8)((v - 4) >> 1); tempo = (u8)(128 + (v > 12 ? 12 : v)); }
        if (sec == 20 && g_boss_state == BS_FIGHT && g_met_tempo) tempo = g_met_tempo;   /* METRONOME: 振り子の周期で(振れ幅では揺らさない) */
        if (sec == 18 && !g_scroll_speed) atten = 15;            /* 急停止: 無音 */
        if (sec == 20 && g_boss_state == BS_DYING) bend = (s8)((mt & 4) ? 1 : -1);   /* 錘が壊れて曲がねじれる */
    } else if (sec == 25) mute = 0x0182;                         /* 4 面: 帯が増えるほどパートが増える */
    else if (sec == 26) mute = 0x0180;
    else if (sec == 27) mute = 0x0100;
    else if (sec == 28) det = (s8)((mt & 32) ? 2 : -2);          /* 陽炎帯: ゆらぐ */
    else if (sec == 47) mute = 0x0007;                           /* 6 面 影: 高い声部が消える */
    else if (sec == 57) det = (s8)(((mt >> 3) & 3) - 1);         /* 8 面 歪曲回廊: ピッチがゆらぐ */
    else if (sec == 67) {                                        /* 終幕: スタッフロールで楽器が減っていく */
        static const u16 drop[6] = { 0, 0x0100, 0x0104, 0x0184, 0x0186, 0x018E };
        u8 k = (u8)(mt >> 9);
        mute = drop[k > 5 ? 5 : k];
    }
    if (sec == 42 && mt == 45) pcm_req_opt(PCM_P03);                 /* 5 面 撃沈: 爆発の後に低いオケヒ */
    g_snd_bend = bend; g_snd_detune = det; g_snd_tempo = tempo; g_snd_atten = atten; g_snd_mute = mute;
}

static void on_sec(u8 sec) {
    if (sec == 52) v04 = 0;
    if ((sec == 54 || sec == 56 || sec == 59 || sec == 62) && !v04) pcm_req(PCM_V04);   /* 8 面: UNMEASURABLE。幕開け(STAGE 8 の表示の間)に。
                                                                     8 面は重く、落ち着かずに捨てたら縦穴・中枢・第 3 形態で頼み直す */
    /* ★重力反転 / 逆走の逆再生シンバル(P04)はやめた: 8kHz では中身(6kHz より上)が出せず、曲の演出(ピッチベンド、
       下降進行)と役割が重なる。曲そのものの逆再生に替える予定(docs/TODO.md) */
    else if (sec == 18) pcm_req_opt(PCM_P03);                        /* 急停止: オケヒ 1 発 */
    if (sec == 30) pcm_req_opt(PCM_P01);                             /* 4 面の大河: RULER の登場にオケヒ */
    if (sec == 60) pcm_req(PCM_V04);                                 /* ラスボス(第 1 形態の始まり): UNMEASURABLE */
}

/* ビブラート(snd.c の vib_t の説明)。揺れ幅は音の高さに比例させる(F-Number・周期 × 段 / 1024)。★`*` `/` は使わない */
static const s8 vib_sin[10] = { 0, 9, 15, 15, 9, 0, -9, -15, -15, -9 };   /* 16 倍、10 フレームで 1 周(6Hz) */
static u16 vib_mul(u16 b, u8 n) { u16 r = 0; while (n--) r += b; return r; }
static void vib_frame(void) {
    s8 f = 0, p = 0;
    if ((ch[10] & 1) && g_vib_depth) {
        if (vib_t < 255) vib_t++;
        if (vib_t > g_vib_delay) {
            u8 k = (u8)(vib_t - g_vib_delay), amp, w, o = 0;
            s8 sn;
            if (k == 1) vib_ph = 0; else if (++vib_ph >= 10) vib_ph = 0;
            amp = k >> 1;
            if (amp > g_vib_depth) amp = g_vib_depth;
            sn = vib_sin[vib_ph];
            w = (u8)(vib_mul(amp, (u8)(sn < 0 ? -sn : sn)) >> 4);
            if (w) {
                s16 n = (s16)ch[6] + g_snd_trans + g_snd_bend;
                u16 bp;
                if (n < 24) n = 24; else if (n > 95) n = 95;
                while (n >= 12) { n -= 12; o++; }
                f = (s8)((vib_mul(fm_fnum[n], w) + 512) >> 10);
                bp = psg_per1[n] >> (o + 2);            /* 周期の 2 倍(o - 1 + 4 - 1 回ずらす) */
                p = (s8)((vib_mul(bp, w) + 1024) >> 11);
                if (sn < 0) f = -f; else p = -p;      /* 周期は短いほど高い */
            }
        }
    }
    if (f != vib_fm || p != vib_psg) { vib_fm = f; vib_psg = p; vib_apply(); }
}

void music_frame(u8 over) {
    u8 w, st = 0, sec = g_sec;
    if (sel_ok != 0x5A) { want = 0xFE; sec_prev = 0xFF; ph_prev = 0xFF; bs_prev = 0; calib_prev = 0; mt = 0; v04 = 0; motif_t = 0; sel_ok = 0x5A;
                         g_vib_delay = 15; g_vib_depth = 20; }
    if (sec >= NSEC) sec = NSEC - 1;
    while (sec >= sec_first[st]) st++;
    if (over) w = SONG_M17;
    else if (g_phase == PH_LOGO) w = SND_NONE;
    else if (g_phase == PH_TITLE) w = g_title_mode == 1 ? SONG_M18 : g_title_mode == 2 ? SONG_M01R : SONG_M01;   /* ネームエントリー / 巻き戻し(タイトルの曲を逆に) */
    else if (g_phase == PH_SORTIE) w = SONG_M02;
    else if (g_calib && g_calib < 7) w = motif_t ? stage_song[g_calib - 1] : SONG_M10;   /* 7 面の試験 01..06(頭でその面の曲を挟む) */
    else {
        w = sec_song[sec];
        if (w == A_) w = st == 7 ? SONG_M10X : SONG_M03;   /* ★8 面の空中戦は 7 面の曲の豪華版(遊んでの頼み。M03 の +8 は高すぎた) */
        else if (w == S_) w = stage_song[st];
        else if (w == MI) w = SONG_M12;
        else if (w == BO) w = SONG_M13;
        else if (w == OU) w = SONG_J01;
        else if (w == CO) w = mt < 110 ? SND_NONE : SONG_M08C;
    }
    if (g_pcm_cur == PCM_V04) v04 = 1;
    if (g_phase != ph_prev) {
        ph_prev = g_phase;
        if (g_phase == PH_LOGO) pcm_req(PCM_V03);
        if (g_phase == PH_SORTIE) pcm_halt();                       /* ★出撃: タイトルの曲のオケヒを止める(押す瞬間に鳴って
                                                                          いると出撃に入っても鳴り続け、開始直後に破裂音が出たり
                                                                          出なかったりした。指摘) */
    }
    /* 試験の声: 1 つ目は測定空間から入るとき、2 つ目からは前の試験のボスを倒した直後(判定文の静かな間。次の試験の
       予告)。★試験の入りは読み込みと降下で重いフレームが続き、そこで鳴らすと遅れた */
    if (g_calib != calib_prev) {
        if (g_calib == 1) pcm_req(PCM_V06);
        if (g_calib && g_calib < 7) motif_t = 150;                /* 試験の頭: その面の曲を 1 小節ほど挟んでから M10(指示書 6.3) */
        calib_prev = g_calib;
    }
    if (motif_t) motif_t--;
    if (g_boss_state == BS_DYING && bs_prev != BS_DYING) {
        if (g_calib && g_calib < 7) pcm_req(PCM_V06); else pcm_req_opt(PCM_P06);
        sfx(SFX_BOOM);
    }
    bs_prev = g_boss_state;
    if (g_phase >= PH_AIR && !over) {
        if (sec != sec_prev) { sec_prev = sec; mt = 0; on_sec(sec); }
        effects(sec);
        mt++;
    } else {
        sec_prev = 0xFF;
        g_snd_bend = 0; g_snd_detune = 0; g_snd_tempo = 128; g_snd_atten = 0; g_snd_mute = 0;
    }
    vib_frame();
    if (w == want) return;
    want = w;
    if (w == SONG_M17) pcm_req(PCM_V05);                         /* ゲームオーバー: 声「GAME OVER」 */
    if (w == SONG_M03 && g_bgm == SONG_M02) return;              /* 出撃のファンファーレが続けて鳴らす */
    if (w == SONG_M13 && g_bgm == SONG_J02) return;
    snd_trans_next = (w == SONG_M03) ? air_trans[st] : 0;   /* 移調は次の曲の頭から(フェード中の曲の音程を変えない) */
    if (w == SONG_M13) { bgm_fade(SONG_J02, 1); pcm_req(PCM_V02); return; }   /* 警告 → ボス曲(短いフェード) */
    bgm_fade(w, g_bgm == SONG_M03 ? 4 : 1);         /* ★フェードアウトしてから: 空中戦 → 面の曲は約 1 秒、ほかは約 0.25 秒 */
}

