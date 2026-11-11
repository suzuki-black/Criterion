/* spr.c — スプライト(spr.h 参照)。
   VRAM の置き場所(M2〜): ★表示リング(ページ 0|1)は縦スクロール(R#23)で 256 行すべてが画面に出るので、
     スプライトの表はページ 3 の 212 行以降(表示しない場所)に置く。
     パターン表 0x1F800-0x1FFFF(64 パターン。63 番は空白 = 使わない枠を隠す)
     属性表 / 色表: 上の帯 A0 0x1EA00/0x1E800、A1 0x1EE00/0x1EC00、下の帯 B0 0x1F200/0x1F000、B1 0x1F600/0x1F400
     (色表は属性表の 0x200 前に自動で決まる。R#11 = 3)
     HUD 帯の表 C0 0x1E600/0x1E400、C1 0x1E200/0x1E000(ゲージのカーソル 1 枚だけ)
   ★Y 座標は「表示の行 + R#23」で比べられるので、spr_voff(プレイエリアの R#23)を足す。
   ★HUD 帯は R#23 が別なので属性表も別にする(同じ表だと、縦スクロール量しだいで HUD のスプライトが
     プレイエリアの行でも一致して描かれた。M2)。
   ★216 は「ここで終わり」の印。足した結果が 216 になったら 217 にする(1 行ずれるだけ)。
   ★spr_put の時点で帯ごとの属性表の写し(RAM)へ直接置き、spr_end は写しを OTIR で送るだけにする。
     要求を貯めてから帯ごとに振り分け直す作りは、SDCC の生成コードでは 1 要求 300 クロック前後かかり、
     スプライトだけで 1 フレームの半分を使った(M1)。 */
#include "spr.h"
#include "vdp.h"
#define G_MISC_N 7                  /* asm 用(spr.h の G_MISC) */

#define SLOTS   32
#define BLANK_PAT (63 * 4)          /* 空白のパターン(使わない枠を隠す) */
#define HUD_VOFF  24                /* HUD 帯の R#23(ページ 2 の 212 行を画面の 188 行に出す) */

/* ★グループの色表は tools/gen_sprites.py が作って ROM に置き(spr_col.bin)、spr_colors で写す(常駐の空きのため。
   単色のグループは作らない。M7 の後)。ここには G_MISC の組(フレームごとに枠の色を替える)だけを持つ */
static const u8 misc_col[CS_COUNT][16] = {
    /* CS_QUANTA */ { 10,10,11,11,10,10,11,11,10,10,11,11,10,10,11,11 },
    /* CS_EXPL   */ { 15,15,11,11,13,13,12,12,12,12,13,13,11,11,15,15 },
    /* CS_CURSOR */ { 15,15,11,11,15,15,11,11,15,15,11,11,15,15,11,11 },
    /* CS_SHARD  */ { 12,15,12,15,12,15,12,15,12,15,12,15,12,15,12,15 },
    /* CS_DIM    */ { 14,14,9,9,14,14,9,9,14,14,9,9,14,14,9,9 },
};

const u16 sat_addr[2][2] = { { 0xEA00, 0xEE00 }, { 0xF200, 0xF600 } };   /* 0x1xxxx の下 16 ビット */

/* ---- 帯の写し(asm から使うので非 static。添字の [帯 * 8 + グループ] は 8 で揃える) ---- */
u8 spr_shadow[2 * SLOTS * 4];        /* 帯ごとの属性表の写し(帯 b は b * 128 から) */
u8 spr_cnt[16];                      /* 帯ごと・グループごとの使用数 */
u8 spr_pos[16];                      /* 次に置く枠(グループの範囲で回る) */
u8 spr_last[2];                      /* 使っている枠の最大 + 1 */
u8 spr_misc_cs[8];                   /* 帯ごとの G_MISC 4 枠の色の組(0xFF = 空き) */
/* 枠: 自機 2、自機弾 6、2 枚重ねの敵 8(4 体)、1 枚の敵 6、敵弾 6、その他 4。★G_PAIR は偶数個・偶数から
   (下の層が偶数の枠、上の層がその次。CC は番号が 1 つ前の CC = 0 の枠と重なる) */
/* ★枠の割り振り(spr_layout)。部品の多いボスの間だけ、1 帯 32 枠のうち自機・自機弾・その他を除く 20 枠を、その場面で
   足りないものへ回す(中ボスの部品が 1 つの帯でグループの枠に入りきらず、消えて見えた。M7 の後)。組を 0 にすると戦闘中に
   来る編隊の敵が丸ごと消えたので、どの割り振りにも組を残す(溢れた分はグループの中で順番を回すのでちらつきに散る)。
     0 = ふつう(組 8・1 枚の敵 6・敵弾 6)
     1 = 組 4・1 枚の敵 10・敵弾 6(3 面 Metronome、4 面 VERNIER の光線)
     2 = 組 2・1 枚の敵 11・敵弾 7(1 面 PLUMB の鎖、2 面 CW)
     3 = 組 6・1 枚の敵 6・敵弾 8(6 面の砂時計の目)
     4 = 組 4・1 枚の敵 4・敵弾 12(4 面 STRATUM、5 面 GAUGE BLOCK、8 面の二重の鐘・脱出の破片)
     5 = 組 2・1 枚の敵 8・敵弾 10(8 面の絶対尺度: 砲台 12 基を上下の帯に分けて + 弾)
     6 = 組 10・1 枚の敵 4・敵弾 6(空中戦: 編隊 5 組が 1 つの帯に並ぶと 8 枠では足りなかった)
     7 = 組 6・1 枚の敵 10・敵弾 4(3 面 METRONOME: 前の区間から残った編隊が横切る)
   ★4〜6 は 1 周の通しで「枠に入りきらなかったグループ」を区間ごとに数えて足した(ボスの前後のちらつきの指摘。溢れると
     グループの中で順番を回すので、毎フレーム違うものが消えてちらつく)。
   色表も割り振りごとに ROM にある(gen_sprites.py の LAYOUTS と同じ数)。asm は下の RAM の表を見る
   8..15 = 強化(ECHO / AEGIS / DROP。分身・防壁)を持っている間: 上の同じ番号から G_OPT の 4 枠を回したもの(main.c が 8 を足す)。 */
static const u8 layouts[16][3] = {    /* 組・1 枚の敵・敵弾の枠の数(自機 1+1・自機弾 6・その他 4 は共通。G_OPT を足して 32) */
    { 8, 6, 6 }, { 4, 10, 6 }, { 2, 11, 7 }, { 6, 6, 8 }, { 4, 4, 12 }, { 2, 8, 10 }, { 10, 4, 6 }, { 6, 10, 4 },
    { 6, 5, 5 }, { 4, 7, 5 }, { 2, 9, 5 }, { 4, 6, 6 }, { 2, 4, 10 }, { 2, 6, 8 }, { 8, 3, 5 }, { 4, 8, 4 },
};
u8 spr_gfirst[8], spr_gend[8], spr_gcount[8];
/* ★枠の並び(番号の小さい枠が 1 ラインに 8 枚の制限に勝つ): 自機 → その他(強化の分身・防壁・爆発・Quanta)→ 自機弾 → 敵 → 敵弾。
   その他を最後(28..31)に置いていた間は、自機 2 枚と自機弾 6 枚が同じ行に並ぶとその行の分身が必ず消えた(指摘「オプションが
   消える」)。自機弾はグループの中で順番を回すので、負けても消えずにちらつく。その他は色を枠ごとに書くので回さない */
#define MISC_FIRST 2
static const u8 slot_order[G_COUNT] = { G_SHIP_A, G_SHIP_B, G_MISC, G_SHOT, G_PAIR, G_ENEMY, G_EBUL, G_OPT };
/* ★溢れたときの窓(asm から使う): グループの枠より多く置かれたら、置く顔ぶれをフレームごとに枠の数ずつずらす(後から置いた
   ものが毎フレーム同じように消えていた。指摘「完全に消えずにちらつかせて」)。帯・グループごとに 4 バイト:
   [0] このフレームに置こうとした数、[1] 窓の始め s(0xFF = 溢れていない)、[2] 先頭から置く数(折り返し分)、[3] その次に飛ばす数。
   フレームの頭(win_steps)で s を枠の数だけ進め、[2][3] を出す。1 枚ごとは [2] か [3] を 1 つ減らすだけ(★1 枚ごとに
   (k - s) mod n を計算すると、弾の多い場面で積み重なり、PCM の鳴る間の遅れが 5 → 13 に増えた) */
u8 spr_win[64];
u8 spr_wcap[8];                      /* グループに入る数(組は 2 枠で 1 つ) */
u8 g_spr_layout;
static u8 spr_step;                  /* spr_add: 1 回に使う枠の数(G_PAIR は 2) */
u8 spr_bx, spr_bp, spr_bg, spr_bcs, spr_bmask;   /* spr_add への受け渡し */
u8 spr_voff;                         /* プレイエリアの縦スクロール(R#23)。Y に足す */

static u8 misc_cache[2][2][4];       /* その表の G_MISC の 4 枠に今書いてある色の組 */
/* その表の枠に今書いてある Y(0xFF = 分からない。sat_send)。★常駐の変数の領域に空きが無いので、ページ 0 の RAM の 0x3C80..0x3CFF
   (ROTOR の写し先 0x3C00..0x3C7F と PCM の置き場 0x3D00.. の間)に置く。spr_init はページ 0 を RAM にする前に呼ばれるので、
   初期化は spr_sat_reset(main.c がページ 0 の用意の後に呼ぶ) */
#define SAT_Y ((u8 *)0xEC80)         /* [帯][表][枠] = SAT_Y + (帯 * 2 + 表) * SLOTS */
void spr_sat_reset(void) { u8 *p = SAT_Y, i = 2 * 2 * SLOTS; do *p++ = 0xFF; while (--i); }
static u8 rot_g[G_COUNT];            /* グループごとの回し始め(毎フレーム +1、数で折り返す) */
static u8 buf;                       /* 次に書く側(0/1) */
u8 g_spr_dropped;
SprRegs spr_regs;

/* HUD 帯の表(1 枚) */
static const u16 hud_sat[2] = { 0xE600, 0xE200 };
static u8 hud_y, hud_x, hud_p, hud_cs, hud_on;
static u8 hud_cache[2];

static u8 r5_of(u16 a) { return (u8)(((a >> 7) & 0xFF) | 0x07); }

static u8 hide_y(void) {
    u8 y = (u8)(spr_voff + 212);     /* プレイエリアの下の外 */
    return y == 216 ? 217 : y;
}

void spr_layout(u8 l) {
    u8 i, at = 0;
    const u8 *s = layouts[l];
    g_spr_layout = l;
    spr_gcount[G_SHIP_A] = 1; spr_gcount[G_SHIP_B] = 1; spr_gcount[G_SHOT] = 6; spr_gcount[G_MISC] = 4;
    spr_gcount[G_PAIR] = s[0]; spr_gcount[G_ENEMY] = s[1]; spr_gcount[G_EBUL] = s[2]; spr_gcount[G_OPT] = l >= 8 ? 4 : 0;
    for (i = 0; i < G_COUNT; i++) {
        u8 g = slot_order[i];
        spr_gfirst[g] = at; at += spr_gcount[g]; spr_gend[g] = at; rot_g[g] = 0;
        spr_wcap[g] = g == G_PAIR ? (u8)(spr_gcount[g] >> 1) : spr_gcount[g];
    }
}

void spr_init(void) {
    u8 i, j, b;
    spr_layout(0);
    for (b = 0; b < 2; b++)
        for (j = 0; j < 2; j++) {
            u16 sa = sat_addr[b][j];
            vdp_write_addr17(1, sa);
            VDP_DAT = 216;                                  /* 空の表 */
            for (i = 0; i < 4; i++) misc_cache[b][j][i] = 0xFF;
        }
    for (i = 0; i < 2 * SLOTS; i++) { spr_shadow[i * 4] = hide_y(); spr_shadow[i * 4 + 2] = BLANK_PAT; }
    hud_cache[0] = hud_cache[1] = 0xFF;
    for (j = 0; j < 2; j++) { vdp_write_addr17(1, hud_sat[j]); VDP_DAT = 216; }
    spr_regs.r5h = r5_of(hud_sat[0]);
    vdp_wreg(6, 0x3F);                                      /* パターン表 0x1F800 */
    *(volatile u8 *)0xF3E0 |= 0x02;                         /* 16x16(RG1SAV と合わせる) */
    vdp_wreg(1, *(volatile u8 *)0xF3E0);
    vdp_wreg(5, r5_of(sat_addr[0][0]));
    vdp_wreg(11, 3);
    spr_regs.r5a = r5_of(sat_addr[0][0]); spr_regs.r11a = 3;
    spr_regs.r5b = r5_of(sat_addr[1][0]); spr_regs.r11b = 3;
    buf = 1;
}

/* 前のフレームに使った枠だけ隠す(A = 隠す Y)。★C では 1 枠 12us、両方の帯で 1 フレームに 0.8ms かかっていた(M7 の後) */
static void spr_hide(u8 y) __naked {
    (void)y;
    __asm
        ld   c, a
        ld   hl, #_spr_shadow
        ld   a, (_spr_last)
        call 00001$
        ld   hl, #_spr_shadow + 128   ; 下の帯(SLOTS * 4)
        ld   a, (_spr_last + 1)
    00001$:
        or   a
        ret  z
        ld   b, a
    00002$:
        ld   (hl), c               ; Y
        inc  hl
        inc  hl
        ld   (hl), #252            ; パターン = 空白(BLANK_PAT = 63 * 4)
        inc  hl
        inc  hl
        djnz 00002$
        ret
    __endasm;
}

/* 窓を枠の数 c だけ進め(n = 前のフレームに置こうとした数。溢れていなければ s = 0xFF)、先頭から置く数 = max(0, s + c - n)、
   飛ばす数 = s - 先頭から置く数 を出す。帯・グループの 16 組すべて(asm。★C で 16 回呼ぶと 1 フレームに 7 ラインかかった) */
static void win_steps(void) __naked {
    __asm
        ld   hl, #_spr_win
        ld   bc, #0x1000           ; B = 16 組、C = 帯 * 8 + グループ
    00001$:
        ld   e, (hl)               ; E = n
        ld   (hl), #0
        push hl
        ld   a, c
        and  #7
        ld   hl, #_spr_wcap
        add  a, l
        ld   l, a
        jr   nc, 00002$
        inc  h
    00002$:
        ld   d, (hl)               ; D = c
        pop  hl
        push hl
        inc  hl                    ; HL = s
        ld   a, d
        cp   e
        jr   c, 00003$             ; c < n: 溢れている
        ld   (hl), #0xFF
        inc  hl
        ld   (hl), #0
        inc  hl
        ld   (hl), #0
        jr   00008$
    00003$:
        ld   a, (hl)               ; 前の s(0xFF なら 0 から)
        inc  a
        jr   z, 00004$
        dec  a
    00004$:
        add  a, d
    00005$:
        cp   e
        jr   c, 00006$
        sub  e
        jr   00005$
    00006$:
        ld   (hl), a               ; s
        add  a, d
        sub  e                     ; s + c - n
        jr   nc, 00007$
        xor  a
    00007$:
        inc  hl
        ld   (hl), a               ; 先頭から置く数
        ld   d, a
        dec  hl
        ld   a, (hl)
        sub  d
        inc  hl
        inc  hl
        ld   (hl), a               ; 飛ばす数
    00008$:
        pop  hl
        inc  hl
        inc  hl
        inc  hl
        inc  hl
        inc  c
        djnz 00001$
        ret
    __endasm;
}

void spr_begin(void) {
    u8 g, st;
    spr_hide(hide_y());
    spr_last[0] = spr_last[1] = 0;
    for (g = 0; g < G_COUNT; g++) {
        u8 c = spr_gcount[g];
        if (c > 1 && g != G_MISC && (rot_g[g] += (g == G_PAIR ? 2 : 1)) >= c) rot_g[g] = 0;   /* 1 ラインの制限のちらつきの分散: グループの中で枠を回す(組は 2 つずつ)。
                                                                 ★G_MISC は回さない(枠ごとの色を毎フレーム書き直すことになり重い)。溢れた分は窓(spr_win)でちらつく */
        st = (u8)(spr_gfirst[g] + rot_g[g]);
        spr_pos[g] = st; spr_pos[8 + g] = st;
        spr_cnt[g] = 0; spr_cnt[8 + g] = 0;
        spr_misc_cs[g] = 0xFF;
    }
    win_steps();
}

/* 帯の写しへ 1 枚置く(asm)。A = 表の Y(表示の上端 - 1)、spr_bx / spr_bp / spr_bg / spr_bcs / spr_bmask。
   ★SDCC の生成コードでは 1 回 150 命令(R800 で 100us 前後)かかり、1 フレームに数十回呼ぶので手で書いた(M1)。 */
void spr_add(u8 yy) __naked {
    (void)yy;
    __asm
        ld   c, a                  ; C = Y
        ld   a, (_spr_bmask)
        rra
        jr   nc, 00001$
        push af
        ld   b, #0
        call 00010$                ; 上の帯
        pop  af
    00001$:
        rra
        ret  nc
        ld   b, #1                 ; 下の帯(そのまま下へ)
    00010$:                        ; B = 帯、C = Y
        ld   a, (_spr_bg)
        cp   #3                    ; G_PAIR: 2 枠
        ld   a, #1
        jr   nz, 00019$
        inc  a
    00019$:
        ld   (_spr_step), a
        ld   a, (_spr_bg)
        ld   e, a
        ld   d, #0                 ; DE = グループ
        ld   a, b
        add  a, a
        add  a, a
        add  a, a
        add  a, e
        ld   l, a
        ld   h, #0                 ; HL = 帯 * 8 + グループ
        push hl
        ; ---- 窓(上の spr_win): 先頭から置く数が残っていれば置く、飛ばす数が残っていれば置かない
        add  hl, hl
        add  hl, hl
        ld   de, #_spr_win
        add  hl, de
        inc  (hl)                  ; 置こうとした数
        inc  hl
        inc  hl
        ld   a, (hl)               ; 先頭から置く数
        or   a
        jr   z, 00031$
        dec  (hl)
        jr   00032$
    00031$:
        inc  hl
        ld   a, (hl)               ; 飛ばす数
        or   a
        jr   z, 00032$
        dec  (hl)
        jp   00090$                ; 窓の外: 今回は置かない(数えるのは溢れと同じ)
    00032$:
        pop  hl
        push hl
        ld   a, (_spr_bg)
        ld   e, a
        ld   d, #0                 ; DE = グループ
        ld   iy, #_spr_gcount
        add  iy, de
        ld   de, #_spr_cnt
        add  hl, de
        ld   a, 0 (iy)             ; 枠の数
        cp   (hl)
        jp   z, 00090$             ; 満杯
        jp   c, 00090$
        ld   a, (_spr_step)
        add  a, (hl)
        ld   (hl), a               ; 使用数 += 枠の数
        pop  hl
        ld   de, #_spr_pos
        add  hl, de
        ld   e, (hl)               ; E = 枠
        ld   a, (_spr_step)
        add  a, e
        ld   d, a                  ; D = 次の枠
        ld   a, (_spr_bg)
        push hl
        ld   hl, #_spr_gend
        add  a, l
        ld   l, a
        jr   nc, 00011$
        inc  h
    00011$:
        ld   a, d
        cp   (hl)
        jr   nz, 00012$
        ld   a, (_spr_bg)
        ld   hl, #_spr_gfirst
        add  a, l
        ld   l, a
        jr   nc, 00013$
        inc  h
    00013$:
        ld   a, (hl)
    00012$:
        pop  hl
        ld   (hl), a               ; 次の枠(折り返し)
        ld   hl, #_spr_last
        ld   a, b
        add  a, l
        ld   l, a
        jr   nc, 00014$
        inc  h
    00014$:
        ld   a, d
        cp   (hl)
        jr   c, 00015$
        ld   (hl), a               ; 使っている枠の最大 + 1
    00015$:
        ld   a, (_spr_bg)
        cp   #G_MISC_N             ; G_MISC
        jr   nz, 00016$
        ld   a, b
        add  a, a
        add  a, a
        add  a, e
        sub  #MISC_FIRST
        ld   hl, #_spr_misc_cs
        add  a, l
        ld   l, a
        jr   nc, 00017$
        inc  h
    00017$:
        ld   a, (_spr_bcs)
        ld   (hl), a
    00016$:
        ld   l, e
        ld   h, #0
        add  hl, hl
        add  hl, hl                ; 枠 * 4
        ld   a, b
        or   a
        jr   z, 00018$
        ld   de, #128
        add  hl, de
    00018$:
        ld   de, #_spr_shadow
        add  hl, de
        ld   (hl), c               ; Y
        inc  hl
        ld   a, (_spr_bx)
        ld   (hl), a               ; X
        inc  hl
        ld   a, (_spr_bp)
        ld   (hl), a               ; パターン
        ld   a, (_spr_step)
        dec  a
        ret  z
        inc  hl                    ; G_PAIR: 次の枠に上の層(パターン + 1)
        inc  hl
        ld   (hl), c
        inc  hl
        ld   a, (_spr_bx)
        ld   (hl), a
        inc  hl
        ld   a, (_spr_bp)
        add  a, #4
        ld   (hl), a
        ret
    00090$:
        pop  hl
        ld   hl, #_g_spr_dropped
        inc  (hl)
        ret
    __endasm;
}

static u8 table_y(s16 y, u8 voff) {
    u8 t = (u8)(y - 1 + voff);       /* 表示は Y+1 行目から。R#23 を足す */
    return t == 216 ? 217 : t;
}

/* ★asm(C では 1 枚約 1 ライン。敵・弾・爆発で 1 フレームに 50 回呼ぶ)。sdcccall(1): HL = x、DE = y、
   スタックに pat / group / cs(呼ばれた側が 3 バイト下ろす) */
void spr_put(s16 x, s16 y, u8 pat, u8 group, u8 cs) __naked {
    (void)x; (void)y; (void)pat; (void)group; (void)cs;
    __asm
        ld   a, h
        or   a
        jr   nz, 00090$            ; x が 0..255 の外
        ld   a, l
        ld   (_spr_bx), a
        ld   hl, #16
        add  hl, de                ; HL = y + 16(y > -16 かつ y < SPR_LIMIT_Y - 8 = 180)
        ld   a, h
        or   a
        jr   nz, 00090$
        ld   a, l
        or   a
        jr   z, 00090$
        cp   #(SPR_LIMIT_Y - 8 + 16)
        jr   nc, 00090$
        ld   b, #0                 ; 帯: y < SPR_SPLIT → 上、y > SPR_SPLIT - 16 → 下
        cp   #(SPR_SPLIT + 16)
        jr   nc, 00001$
        inc  b
    00001$:
        cp   #(SPR_SPLIT + 1)
        jr   c, 00002$
        set  1, b
    00002$:
        ld   a, b
        ld   (_spr_bmask), a
        ld   hl, #2
        add  hl, sp                ; pat / group / cs
        ld   a, (hl)
        add  a, a
        add  a, a
        ld   (_spr_bp), a
        inc  hl
        ld   a, (hl)
        ld   (_spr_bg), a
        inc  hl
        ld   a, (hl)
        ld   (_spr_bcs), a
        ld   a, (_spr_voff)        ; 表の Y = y - 1 + R#23(216 は 217 に)
        add  a, e
        dec  a
        cp   #216
        jr   nz, 00003$
        inc  a
    00003$:
        call _spr_add
    00090$:
        pop  hl
        pop  af
        inc  sp
        jp   (hl)
    __endasm;
}

void spr_put_hud(s16 x, s16 y, u8 pat, u8 cs) {
    if ((u16)x > 255 || y < HUD_SCREEN_Y - 16 || y >= 212) return;
    hud_y = table_y(y, HUD_VOFF); hud_x = (u8)x; hud_p = (u8)(pat << 2); hud_cs = cs; hud_on = 1;
}

static void write_hud(void) {
    u16 sa = hud_sat[buf];
    vdp_write_addr17(1, sa);
    if (hud_on) { VDP_DAT = hud_y; VDP_DAT = hud_x; VDP_DAT = hud_p; VDP_DAT = 0; }
    VDP_DAT = 216;
    if (hud_on && hud_cache[buf] != hud_cs) {
        vdp_write_addr17(1, (u16)(sa - 0x200u));
        vdp_out_block(16, misc_col[hud_cs]);
        hud_cache[buf] = hud_cs;
    }
    hud_on = 0;
}

/* 帯の属性表を送る(asm)。★隠した枠(Y = 隠す Y)で、その表にも同じ Y が書いてあるものは送らない(番地を貼り直して飛ばす)。
   4 面のデモの空中戦で、送る 42 枠のうち使っているのは 18 枠で、毎フレーム 1 回 8.6us の I/O を約 86 回むだにしていた。
   SAT_Y はその表の枠ごとに最後に書いた Y(spr_sat_reset で 0xFF)。属性表の VRAM は sat_send と spr_init のほかでは書かない。
   入口: 番地は枠 0 に貼ってある(R#14 も)。ss_src = 写し、ss_y = SAT_Y の表の行、ss_n = 枠の数、ss_lo = 枠 0 の番地の下 16 ビット。
   枠の数が SLOTS 未満なら、その枠に 216(表の終わり)を書く(同じなら書かない)。PCM の送出口は 4 枠書くごと */
static const u8 *ss_src;
static u8 *ss_y;
static u8 ss_n, ss_hide;
static u16 ss_lo;
static void sat_send(void) __naked {
    __asm
        ld   c, #4                 ; C = 送出口までの枠の数
        ld   hl, (_ss_src)
        ld   de, (_ss_y)
        ld   a, (_ss_n)
        or   a
        jr   z, 00020$
        ld   b, a                  ; B = 残りの枠
    00001$:                        ; 番地は今の枠を指している
        ld   a, (_ss_hide)
        cp   (hl)                  ; 今は隠している?
        jr   nz, 00003$
        ld   a, (de)
        cp   (hl)                  ; 表にも同じ Y?
        jr   z, 00010$
    00003$:
        ld   a, (hl)
        ld   (de), a
        out  (0x98), a
        inc  hl
        ld   a, (hl)
        out  (0x98), a
        inc  hl
        ld   a, (hl)
        out  (0x98), a
        inc  hl
        ld   a, (hl)
        out  (0x98), a
        inc  hl
        inc  de
        dec  c
        call z, 00040$
        djnz 00001$
    00020$:                        ; 終わり(番地は合っている)
        ld   a, (_ss_n)
        cp   #SLOTS
        ret  nc
        ld   a, (de)
        cp   #216
        ret  z
        ld   a, #216
        ld   (de), a
        out  (0x98), a
        ret
    00010$:                        ; 飛ばしている間(番地はずれたまま)
        inc  hl
        inc  hl
        inc  hl
        inc  hl
        inc  de
        dec  b
        jr   z, 00021$
        ld   a, (_ss_hide)
        cp   (hl)
        jr   nz, 00012$
        ld   a, (de)
        cp   (hl)
        jr   z, 00010$
    00012$:
        call 00030$
        jr   00003$
    00021$:                        ; 飛ばしたまま終わった
        ld   a, (_ss_n)
        cp   #SLOTS
        ret  nc
        ld   a, (de)
        cp   #216
        ret  z
        ld   a, #216
        ld   (de), a
        call 00030$
        ld   a, #216
        out  (0x98), a
        ret
    00030$:                        ; 番地を ss_lo + (ss_n - B) * 4 へ貼り直す(R#14 はそのまま)
        push hl
        push de
        ld   a, (_ss_n)
        sub  b
        ld   l, a
        ld   h, #0
        add  hl, hl
        add  hl, hl
        ld   de, (_ss_lo)
        add  hl, de
        di
        ld   a, l
        out  (0x99), a
        ld   a, h
        and  #0x3F
        or   #0x40
        out  (0x99), a
        ei
        pop  de
        pop  hl
        ret
    00040$:                        ; PCM の送出口(4 枠ごと)
        ld   c, #4
        push hl
        push de
        push bc
        ld   hl, (_g_vdp_wait_hook)
        ld   a, h
        or   l
        call nz, 00031$
        pop  bc
        pop  de
        pop  hl
        ret
    00031$:
        jp   (hl)
    __endasm;
}

static void write_band(u8 b) {
    u16 sa = sat_addr[b][buf];
    u8 i;
    vdp_write_addr17(1, sa);
    ss_src = &spr_shadow[b ? SLOTS * 4 : 0]; ss_y = SAT_Y + (u8)((u8)(b * 2 + buf) * SLOTS); ss_n = spr_last[b]; ss_hide = hide_y(); ss_lo = sa;
    sat_send();
    /* G_MISC の色: 組が変わった枠だけ。★1 フレームに 1 枠まで(溢れて窓がずれると 4 枠とも替わることがあり、1 枠 ≒ 170us。
       PCM の鳴る間に重なって遅れた)。残りは次のフレームに書く(ちらつくものが一瞬違う色になるだけ) */
    for (i = 0; i < 4; i++) {
        u8 cs = spr_misc_cs[b * 4 + i];
        if (cs != 0xFF && misc_cache[b][buf][i] != cs) {
            vdp_write_addr17(1, (u16)(sa - 0x200u + (u16)(MISC_FIRST + i) * 16u));
            vdp_out_block(16, misc_col[cs]);
            misc_cache[b][buf][i] = cs;
            break;
        }
    }
}

u8 spr_written(void) { return (u8)(buf ^ 1); }   /* spr_end が最後に書いた側(0/1) */
u8 spr_pairs(u8 l) { return layouts[l][0]; }      /* 割り振り l の組(2 枚重ね)の枠の数 */

void spr_end(void) {
    write_band(0);
    write_band(1);
    write_hud();
    spr_regs.r5h = r5_of(hud_sat[buf]);
    spr_regs.r5a = r5_of(sat_addr[0][buf]);
    spr_regs.r11a = 3;
    spr_regs.r5b = r5_of(sat_addr[1][buf]);
    spr_regs.r11b = 3;
    buf ^= 1;
}
