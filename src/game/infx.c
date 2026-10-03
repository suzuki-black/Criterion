/* infx.c — ∞ の紋章を ROTOR(スプライト用、rotor_1bpp32)で回して見せる(指示書 2.3 / 4.2)。
   ・確定の演出(infx_start): ∞ゲージで強化を確定したとき、∞ の形が回りながら大きくなって画面の中央へ飛び出し、
     点滅して消える(48 フレーム)。
   ・タイトルの紋章(infx_title): ロゴの下で ∞ がゆっくり回り続ける。
   32x32 の 1bpp を何フレームかで 1 枚作り、スプライトのパターン 52..55 / 56..59 へ交互に書いて、作り終えた方を
   G_MISC の 4 枠で出す(書いている方は表示に使っていないので崩れない)。
   ★ROTOR のスプライト用は 1 ドット約 11.5us(実測)。8 行 × 32 ドットで約 47 ライン、1 枚 12ms。
     1 フレームに 8 行(15Hz で 1 枚)、フレームの残りが少なければその行は次へ回す。転送は 128 バイト(約 18 ライン)。 */
#include "game.h"
#include "vdp.h"
#include "spr.h"
#include "rotor.h"
#include "sys.h"

extern u16 g_frame_t0;            /* main.c: このフレームの始まり */
#define INFX_DEADLINE (150u * 16u)
#define SPRITE_INF32
#include "sprite_data.h"

#define PAT_A   52                 /* パターン 52..55 と 56..59 を交互に */
#define FX_LEN  48

u8 g_infx_mode;                   /* 0 = なし、1 = 確定の演出、2 = タイトル */
#define mode g_infx_mode
static u8  t, row, angle, ready, shown_set, set_next;
static u16 step;
static s16 x0, y0, cx, cy;
static u8  pat_buf[128];           /* 4 枚分のスプライトのパターン(転送待ち) */

static void begin(void) {
    rotor_1bpp_src(spr_inf32);
    t = 0; row = 0; angle = 0; ready = 0; shown_set = 0xFF; set_next = 0;
}

void infx_start(s16 x, s16 y) { mode = 1; x0 = x; y0 = y; begin(); }
void infx_title(u8 on) { if (on) { mode = 2; x0 = 128; y0 = 140; begin(); } else if (mode == 2) mode = 0; }
void infx_reset(void) { mode = 0; }

/* 行優先の 32x32(1 行 4 バイト)→ 16x16 のスプライト 4 枚(左上・右上・左下・右下。1 枚は左 8 列 16 行 →
   右 8 列 16 行の順) */
static void to_patterns(void) {
    u8 q, h, r;
    u8 *d = pat_buf;
    for (q = 0; q < 4; q++)
        for (h = 0; h < 2; h++)
            for (r = 0; r < 16; r++) *d++ = ROT1_DST[(u16)(((q >> 1) * 16u + r) * 4u + (q & 1) * 2u + h)];
}

/* 進める(論理の段): 位置と倍率 */
void infx_update(void) {
    if (!mode) return;
    if (mode == 1) {
        u8 k = t < 32 ? t : 32;
        if (++t >= FX_LEN) { mode = 0; return; }
        cx = (s16)(x0 + (((128 - x0) * (s16)k) >> 5));
        cy = (s16)(y0 + (((80 - y0) * (s16)k) >> 5));
        step = (u16)(1024u - (u16)k * 24u);                    /* 0.25 倍 → 1 倍 */
    } else {
        cx = x0; cy = y0; step = 256; t++;
    }
}

/* VRAM の窓で(スプライトの段の後): 出来た 4 枚をパターン表へ写し、次の 1 枚を何行か作る。
   ★ROTOR の計算はここ(論理の段で回すと、フレームの途中の締め切りでスプライトの段が毎回飛ばされた) */
void infx_vram(void) {
    u8 n;
    if (!mode) return;
    if (ready) {
        vdp_write_addr17(1, (u16)(0xF800u + (u16)(PAT_A + set_next * 4) * 32u));
        vdp_out_block(128, pat_buf);
        shown_set = set_next; set_next ^= 1; ready = 0;
    }
    if ((u16)(sys_timer() - g_frame_t0) > INFX_DEADLINE) return;   /* 重いフレーム: 計算は次へ */
    n = 8;
    if (row == 0) angle = (u8)(angle + (mode == 1 ? 4 : 2));
    rot1_hi = 0x38;
    rotor_1bpp32(ROT1_DST, angle, step, row, n);
    row += n;
    if (row >= 32) { row = 0; to_patterns(); ready = 1; }
}

/* スプライトの段で */
void infx_draw(void) {
    u8 p;
    if (!mode || shown_set == 0xFF) return;
    if (mode == 1 && t > FX_LEN - 12 && (t & 2)) return;       /* 終わりは点滅 */
    p = (u8)(PAT_A + shown_set * 4);
    spr_put(cx - 16, cy - 16, p, G_MISC, CS_SHARD);
    spr_put(cx,      cy - 16, (u8)(p + 1), G_MISC, CS_SHARD);
    spr_put(cx - 16, cy,      (u8)(p + 2), G_MISC, CS_SHARD);
    spr_put(cx,      cy,      (u8)(p + 3), G_MISC, CS_SHARD);
}
