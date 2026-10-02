/* spr.c — スプライト(spr.h 参照)。
   VRAM の置き場所(SCREEN5、表示リングはページ 0|1 の 0..211 行):
     パターン表 0xF000-0xF7FF(64 パターン)
     属性表 / 色表: 上の帯 A0 0x7600/0x7400、A1 0x7E00/0x7C00、下の帯 B0 0xEE00/0xEC00、B1 0xFE00/0xFC00
     (どれも表示されない 212 行以降。色表は属性表の 0x200 前に自動で決まる)
   ★spr_put の時点で帯ごとの属性表の写し(RAM)へ直接置き、spr_end は写しを OTIR で送るだけにする。
     要求を貯めてから帯ごとに振り分け直す作りは、SDCC の生成コードでは 1 要求 300 クロック前後かかり、
     スプライトだけで 1 フレームの半分を使った(M1)。 */
#include "spr.h"
#include "vdp.h"

#define SLOTS   32
#define HIDE_Y  213                 /* 使わない枠(216 は「ここで終わり」の印なので使わない) */

/* グループの色(1 行ずつ。bit6 = CC: 1 つ前の枠と OR 合成) */
static const u8 g_col[G_COUNT - 1][16] = {
    /* SHIP_A */ { 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9 },
    /* SHIP_B */ { 0x4A,0x4A,0x4A,0x4A,0x4A,0x4A,0x4A,0x4A,0x4A,0x4A,0x4A,0x4A,0x4A,0x4A,0x4A,0x4A },
    /* SHOT   */ { 15,11,15,11,15,11,15,11,15,11,15,11,15,11,15,11 },
    /* ENEMY  */ { 12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12 },
    /* EBUL   */ { 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8 },
};
static const u8 misc_col[CS_COUNT][16] = {
    /* CS_QUANTA */ { 10,10,11,11,10,10,11,11,10,10,11,11,10,10,11,11 },
    /* CS_EXPL   */ { 15,15,11,11,9,9,8,8,8,8,9,9,11,11,15,15 },
    /* CS_CURSOR */ { 15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15 },
    /* CS_SHARD  */ { 12,15,12,15,12,15,12,15,12,15,12,15,12,15,12,15 },
    /* CS_DIM    */ { 14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14 },
};

static const u16 sat_addr[2][2] = { { 0x7600, 0x7E00 }, { 0xEE00, 0xFE00 } };

/* ---- 帯の写し(asm から使うので非 static。添字の [帯 * 8 + グループ] は 8 で揃える) ---- */
u8 spr_shadow[2 * SLOTS * 4];        /* 帯ごとの属性表の写し(帯 b は b * 128 から) */
u8 spr_cnt[16];                      /* 帯ごと・グループごとの使用数 */
u8 spr_pos[16];                      /* 次に置く枠(グループの範囲で回る) */
u8 spr_last[2];                      /* 使っている枠の最大 + 1 */
u8 spr_misc_cs[8];                   /* 帯ごとの G_MISC 4 枠の色の組(0xFF = 空き) */
const u8 spr_gfirst[8] = { 0, 1, 2, 10, 22, 28, 0, 0 };
const u8 spr_gend[8]   = { 1, 2, 10, 22, 28, 32, 0, 0 };
const u8 spr_gcount[8] = { 1, 1, 8, 12, 6, 4, 0, 0 };
u8 spr_bx, spr_bp, spr_bg, spr_bcs, spr_bmask;   /* spr_add への受け渡し */

static u8 misc_cache[2][2][4];       /* その表の G_MISC の 4 枠に今書いてある色の組 */
static u8 rot_g[G_COUNT];            /* グループごとの回し始め(毎フレーム +1、数で折り返す) */
static u8 buf;                       /* 次に書く側(0/1) */
u8 g_spr_dropped;
SprRegs spr_regs;

static u8 r5_of(u16 a) { return (u8)(((a >> 7) & 0xFF) | 0x07); }

void spr_init(const u8 (*pat)[32], u8 npat) {
    u8 i, j, b, g;
    vdp_write_addr17(0, 0xF000);
    for (i = 0; i < npat; i++) vdp_out_block(32, pat[i]);
    for (b = 0; b < 2; b++)
        for (j = 0; j < 2; j++) {
            u16 sa = sat_addr[b][j];
            vdp_write_addr17(0, sa);
            VDP_DAT = 216;                                  /* 空の表 */
            vdp_write_addr17(0, (u16)(sa - 0x200u));        /* 色表: グループの色を固定で書く */
            for (g = 0; g < G_MISC; g++)
                for (i = 0; i < spr_gcount[g]; i++) vdp_out_block(16, g_col[g]);
            for (i = 0; i < 4; i++) misc_cache[b][j][i] = 0xFF;
        }
    for (i = 0; i < 2 * SLOTS; i++) spr_shadow[i * 4] = HIDE_Y;
    vdp_wreg(6, 0x1E);                                      /* パターン表 0xF000 */
    *(volatile u8 *)0xF3E0 |= 0x02;                         /* 16x16(RG1SAV と合わせる) */
    vdp_wreg(1, *(volatile u8 *)0xF3E0);
    vdp_wreg(5, r5_of(sat_addr[0][0]));
    vdp_wreg(11, 0);
    spr_regs.r5a = r5_of(sat_addr[0][0]); spr_regs.r11a = 0;
    spr_regs.r5b = r5_of(sat_addr[1][0]); spr_regs.r11b = 1;
    buf = 1;
}

void spr_begin(void) {
    u8 b, g, n, st;
    for (b = 0; b < 2; b++) {
        u8 *sh = &spr_shadow[b ? SLOTS * 4 : 0];
        for (n = spr_last[b]; n; n--, sh += 4) *sh = HIDE_Y;   /* 前のフレームに使った枠だけ隠す */
        spr_last[b] = 0;
    }
    for (g = 0; g < G_COUNT; g++) {
        u8 c = spr_gcount[g];
        if (c > 1 && ++rot_g[g] >= c) rot_g[g] = 0;            /* ちらつきの分散: グループの中で回す */
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
        jr   z, 00090$             ; 満杯
        jr   c, 00090$
        inc  (hl)
        pop  hl
        ld   de, #_spr_pos
        add  hl, de
        ld   e, (hl)               ; E = 枠
        ld   a, e
        inc  a
        ld   d, a
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
        ld   a, e
        inc  a
        cp   (hl)
        jr   c, 00015$
        ld   (hl), a               ; 使っている枠の最大 + 1
    00015$:
        ld   a, (_spr_bg)
        cp   #5                    ; G_MISC
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
        ret
    00090$:
        pop  hl
        ld   hl, #_g_spr_dropped
        inc  (hl)
        ret
    __endasm;
}

void spr_put(s16 x, s16 y, u8 pat, u8 group, u8 cs) {
    if ((u16)x > 255 || y <= -16 || y >= SPR_LIMIT_Y - 8) return;
    spr_bx = (u8)x; spr_bp = (u8)(pat << 2); spr_bg = group; spr_bcs = cs;
    spr_bmask = (u8)((y < SPR_SPLIT ? 1 : 0) | (y > SPR_SPLIT - 16 ? 2 : 0));
    spr_add((u8)(y - 1));                   /* 表示は Y+1 行目から */
}

void spr_put_hud(s16 x, s16 y, u8 pat, u8 cs) {
    if ((u16)x > 255 || y <= -16 || y >= 212) return;
    spr_bx = (u8)x; spr_bp = (u8)(pat << 2); spr_bg = G_MISC; spr_bcs = cs;
    spr_bmask = 2;                          /* HUD は下の帯 */
    spr_add((u8)(y - 1));
}

static void write_band(u8 b) {
    u16 sa = sat_addr[b][buf];
    u8 n = spr_last[b], i;
    vdp_write_addr17(0, sa);
    if (n) vdp_out_block((u8)(n * 4), &spr_shadow[b ? SLOTS * 4 : 0]);
    if (n < SLOTS) VDP_DAT = 216;
    /* G_MISC の色: 組が変わった枠だけ */
    for (i = 0; i < 4; i++) {
        u8 cs = spr_misc_cs[b * 4 + i];
        if (cs != 0xFF && misc_cache[b][buf][i] != cs) {
            vdp_write_addr17(0, (u16)(sa - 0x200u + (u16)(spr_gfirst[G_MISC] + i) * 16u));
            vdp_out_block(16, misc_col[cs]);
            misc_cache[b][buf][i] = cs;
        }
    }
}

void spr_end(void) {
    write_band(0);
    write_band(1);
    spr_regs.r5a = r5_of(sat_addr[0][buf]);
    spr_regs.r11a = 0;
    spr_regs.r5b = r5_of(sat_addr[1][buf]);
    spr_regs.r11b = 1;
    buf ^= 1;
}
