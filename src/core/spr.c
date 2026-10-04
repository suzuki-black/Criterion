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
const u8 spr_gfirst[8] = { 0, 1, 2, 8, 16, 22, 28, 0 };
const u8 spr_gend[8]   = { 1, 2, 8, 16, 22, 28, 32, 0 };
const u8 spr_gcount[8] = { 1, 1, 6, 8, 6, 6, 4, 0 };
static u8 spr_step;                  /* spr_add: 1 回に使う枠の数(G_PAIR は 2) */
u8 spr_bx, spr_bp, spr_bg, spr_bcs, spr_bmask;   /* spr_add への受け渡し */
u8 spr_voff;                         /* プレイエリアの縦スクロール(R#23)。Y に足す */

static u8 misc_cache[2][2][4];       /* その表の G_MISC の 4 枠に今書いてある色の組 */
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

void spr_init(void) {
    u8 i, j, b;
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

void spr_begin(void) {
    u8 b, g, n, st;
    for (b = 0; b < 2; b++) {
        u8 *sh = &spr_shadow[b ? SLOTS * 4 : 0];
        u8 hy = hide_y();
        for (n = spr_last[b]; n; n--, sh += 4) { sh[0] = hy; sh[2] = BLANK_PAT; }   /* 前のフレームに使った枠だけ隠す */
        spr_last[b] = 0;
    }
    for (g = 0; g < G_COUNT; g++) {
        u8 c = spr_gcount[g];
        if (c > 1 && (rot_g[g] += (g == G_PAIR ? 2 : 1)) >= c) rot_g[g] = 0;   /* ちらつきの分散: グループの中で回す(組は 2 つずつ) */
        st = (u8)(spr_gfirst[g] + rot_g[g]);
        spr_pos[g] = st; spr_pos[8 + g] = st;
        spr_cnt[g] = 0; spr_cnt[8 + g] = 0;
    }
    for (g = 0; g < 8; g++) spr_misc_cs[g] = 0xFF;
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
        cp   #6                    ; G_MISC
        jr   nz, 00016$
        ld   a, b
        add  a, a
        add  a, a
        add  a, e
        sub  #28
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

static void write_band(u8 b) {
    u16 sa = sat_addr[b][buf];
    u8 n = spr_last[b], i;
    vdp_write_addr17(1, sa);
    if (n) vdp_out_block((u8)(n * 4), &spr_shadow[b ? SLOTS * 4 : 0]);
    if (n < SLOTS) VDP_DAT = 216;
    /* G_MISC の色: 組が変わった枠だけ */
    for (i = 0; i < 4; i++) {
        u8 cs = spr_misc_cs[b * 4 + i];
        if (cs != 0xFF && misc_cache[b][buf][i] != cs) {
            vdp_write_addr17(1, (u16)(sa - 0x200u + (u16)(spr_gfirst[G_MISC] + i) * 16u));
            vdp_out_block(16, misc_col[cs]);
            misc_cache[b][buf][i] = cs;
        }
    }
}

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
