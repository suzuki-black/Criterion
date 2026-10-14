/* s8b.c — 8 面「原器」の後半のオーバレイ: 中枢・ラスボス PROTOTYPE ZERO(3 形態)・脱出(指示書 5.8)。
   ・中枢: 回廊を進む間に、二重の鐘のコマを作ってページ 2 に置く(外の鐘 96x96 × 4、内の鐘 32x32 × 8 は ROTOR)。
     原器の間でリングのページ 0 = 画面になる位置で止める(LINE の座標 = 画面の座標)。
   ・第 1 形態「二重の鐘」: 外の鐘と内の鐘を逆へ回す(コマを入れ替えて写す)。継ぎ目からレーザー(弾)が漏れる。
   ・第 2 形態「原器」: X 字断面の棒。LINE の 3 本の平行線と両端の十字で描き、回りながら伸び縮みする(遠ざかり、
     近づき)。触れると被弾。両端の目盛りから撃つ。
   ・第 3 形態「絶対尺度」: 目盛りのリング(12 角形の LINE と、目盛りの砲台 12 基のスプライト)が回りながら縮む。
     判定文が MEASURING → VALUE: 9999999 → VALUE OUT OF RANGE と替わる。★とどめは LIMIT BREAK だけ(撃っても
     体力は 1 で止まる)。Infinity Shard が出る。
   ・脱出: 最高速(8 ドット/フレーム)で崩れる回廊を抜ける。全部の帯で破片が降る。宇宙へ上がって終幕へ。
   ★地面(短冊と地図)は前半(s8)のものをそのまま使う(ovl_init で捨てない)。 */
#include "../s8/s8.h"
#include "ovl.h"
#include "vdp.h"
#include "sys.h"
#include "rom.h"
#include "ghost.h"
#include "rotor.h"
#include "spr.h"
#include "sprite_data.h"
#include "assets.h"

extern u16 g_frame_t0;
#include "s8_data.h"
#include "font.h"

#define CX 128
#define CY 94
#define ESC_LEN 2560

static u16 sec_t, sec_wx, t;
static u8  b_t;
static const u8 inside[3] = { 1, 1, 2 }, flash[3] = { 7, 7, 7 };

/* 自機の弾は 1 フレームに 1 回だけ座標にしておく(砲台 12 基 × 弾 12 発を毎回求めると 3ms かかった) */
static u8 shx[MAX_PSHOT], shy[MAX_PSHOT], shi[MAX_PSHOT], nsh;
static void shots_prepare(void) {
    u8 i;
    nsh = 0;
    for (i = 0; i < MAX_PSHOT; i++) {
        const Bullet *b = &g_pshot[i];
        s16 x, y;
        if (!b->on) continue;
        x = PX(b->x); y = PX(b->y);
        if (x < 0 || x > 255 || y < 0 || y > 255) continue;
        shx[nsh] = (u8)x; shy[nsh] = (u8)y; shi[nsh] = i; nsh++;
    }
}
/* ★比較は 8 ビットの引き算 1 回ずつ((u8)(弾 - x) < w。x が画面の少し外でも 256 で回るので同じ。16 ビットで 4 回比べていた間は
   砲台 12 基 × 弾で第 3 形態の 1 フレームの 3% を使い、m7 の遅れが固まった) */
static u8 shot_rect(s16 x, s16 y, u8 w, u8 h) {
    u8 k, n = 0, x8 = (u8)x, y8 = (u8)y;
    const u8 *px = shx, *py = shy;
    for (k = 0; k < nsh; k++, px++, py++) {
        Bullet *b;
        if ((u8)(*px - x8) >= w || (u8)(*py - y8) >= h) continue;
        b = &g_pshot[shi[k]];
        if (!b->on) continue;
        if (!b->pierce) b->on = 0;
        n++;
    }
    return n;
}
/* ★パレットアニメ(M7 の後): 板の縁(色 3)が中枢の鼓動のように 2 度ずつ光る。ボスの間は速く、脱出は赤い警報。宇宙へ抜けたら雲の色へ戻す */
static const u8 beat[16] = { 7, 5, 3, 2, 6, 4, 3, 2, 1, 1, 1, 1, 1, 1, 1, 1 };
static void pulse(void) {
    u8 v, r;
    if (g_sec >= SEC8_OUT) { if (sec_t == 1) { bg_mix(15); bg_mix(16); } return; }   /* bg_mix は値が替わったときだけ書く */
    if (sec_t & 1) return;
    v = (u8)(sec_t >> 1);
    if (g_sec < SEC8_BOSS1) v >>= 1;
    v = beat[v & 15]; r = (u8)(v >> 1);
    if (g_sec == SEC8_ESCAPE) { r = v; v = 0; }                   /* 赤い警報(1 つの呼び出しにまとめる。SDCC 4.6 は 0 の引数で ld hl, a を出した) */
    vdp_set_pal(3, r, v, v);
}

static s16 col_of_screen(void) { return (s16)(((u16)(g_wx - (g_w0 << 3))) >> 3); }

/* ======== 鐘のコマ(中枢の間に作る) ======== */
/* 外の鐘 96x96 × 4(★C3: 64x64 × 8 から大きくした。VRAM に入るのは 4 コマ = 0..33.75°、継ぎ目 8 本なので一周)、
   内の鐘 32x32 × 8(ROTOR)。置き場はページ 2: 外 k → x (k&1)*96、行 (k>>1)*96、内 i → x 192 + (i&1)*32、行 (i>>1)*32 */
#define BO_N   96
#define BO_NF  4
#define RJ_IN  BO_NF               /* 内の鐘の最初の仕事 */
#define RJ_END (BO_NF + 8)
static u8 rj, rph, rrow;           /* 0..3 外の鐘(gen_stage8.py で描いた絵を ROM から写すだけ。ROTOR で 1.5 倍にすると縁が角ばる)、
                                      4..11 内の鐘(ROTOR) */
static void rot_update(void) {
    if (rj >= RJ_END) return;
    if (rph == 0) {
        rrow = 0;
        if (rj < RJ_IN) { rph = 2; return; }
        if (rj == RJ_IN) rom_to_ram(ASSET_BELL_I_BANK, ASSET_BELL_I_OFF, ROTOR_SRC, 2048);
        rotor_begin(32, (u8)((rj - RJ_IN) * 2), 512, 1);       /* 内は 90° で一周(継ぎ目 4) */
        rph = 1;
    } else if (rph == 1) {
        rotor_rows(ROTOR_DST + (u16)rrow * 16u, 8);
        rrow += 8;
        if (rrow >= 32) { rph = 2; rrow = 0; }
    }
}
static void rot_vram(void) {
    u8 k, n = rj < RJ_IN ? BO_N : 32;
    if (rj >= RJ_END || rph != 2) return;
    for (k = 0; k < 8 && rrow < n; k++, rrow++) {
        if (rj >= RJ_IN) {
            u8 i = (u8)(rj - RJ_IN);
            vdp_write_addr17(1, (u16)((u16)((i >> 1) * 32u + rrow) * 128u + 96u + (i & 1) * 16u));
            vdp_out_block(16, ROTOR_DST + (u16)rrow * 16u);
        } else
            rom_to_vram(ASSET_BELL96_BANK, (u16)(ASSET_BELL96_OFF + ((u16)rj * BO_N + rrow) * (BO_N / 2)), 1,
                        (u16)((u16)((rj >> 1) * 96u + rrow) * 128u + (rj & 1) * 48u), BO_N / 2);
    }
    if (rrow >= n) { rj++; rph = 0; }
}

/* ======== 第 1 形態: 二重の鐘 ======== */
static u8 fo, fi;
static void bell_update(void) {
    if (!(t & 7)) fo = (u8)((fo + 1) & 3);                   /* 外は 8 フレームで 11.25°(前の 4 フレームで 5.6° と同じ速さ) */
    if (!(t & 3)) fi = (u8)((fi - 1) & 7);
    if (!(t & 31)) {                                          /* 継ぎ目からレーザー(4 方向、回る) */
        u8 a = (u8)(t >> 3), k;
        for (k = 0; k < 4; k++) {
            u8 b = (u8)(a + k * 16);
            enemy_bullet(FX(CX), FX(CY), (s16)(g_sin64[(u8)(b + 16) & 63] >> 2), (s16)(g_sin64[b & 63] >> 2));
        }
    }
    if (!(t & 63)) enemy_shoot(FX(CX), FX(CY));
    g_boss_aim = CY;
    if (shot_rect(CX - 36, CY - 36, 72, 72)) {
        if (g_boss_hp) g_boss_hp--;
        if (!g_boss_hp) { g_boss_state = BS_DYING; b_t = 0; score_add(4000); }
    }
}
static void bell_in(void) { ring_cmd((u16)(192u + (fi & 1) * 32u), (u16)(512u + (fi >> 1) * 32u), CX - 16, CY - 16, 32, 32, 0, 0); }
static void bell_vram(void) {      /* 外は替わったら 32 行ずつ 3 回。内は真ん中の片(内の鐘を覆う)のすぐ後と、その 4 フレーム後 */
    u8 p = (u8)(t & 7);
    if (gq_free() < 8 || (p & 1)) return;
    if (p == 6) { bell_in(); return; }
    ring_cmd((u16)((fo & 1) * 96u), (u16)(512u + (fo >> 1) * 96u + p * 16u), CX - 48, (s16)(CY - 48 + p * 16), 96, 32, 0, 0);
    if (p == 2) bell_in();
}

/* ======== LINE の図形(第 2 形態の棒、第 3 形態のリング): 前の線を消して描く ======== */
#define NL 28                      /* 線の数(リングの行の継ぎ目で 2 本に分けることがある)*/
static s16 lx[NL], ly[NL], ldx[NL], ldy[NL];
static u8  ln;
static void line_erase(void) { u8 k; for (k = 0; k < ln; k++) gq_line((u16)lx[k], (u8)(ly[k] + g_vscroll), ldx[k], ldy[k], 1); ln = 0; }
static void seg(s16 x0, s16 y0, s16 x1, s16 y1, u8 col) {
    if (ln >= NL) return;
    gq_line((u16)x0, (u8)(y0 + g_vscroll), (s16)(x1 - x0), (s16)(y1 - y0), col);   /* リングの行 = 画面の行 + R#23(ページ 0) */
    lx[ln] = x0; ly[ln] = y0; ldx[ln] = (s16)(x1 - x0); ldy[ln] = (s16)(y1 - y0); ln++;
}
/* 画面の中に収まる線(端を詰める)。★リングの行が 255 → 0 に戻る行をまたぐ線は 2 本に分ける(LINE はページを越える) */
static void line_add(s16 x0, s16 y0, s16 x1, s16 y1, u8 col) {
    s16 yw = (s16)(256 - g_vscroll);
    if (x0 < 0) x0 = 0; if (x0 > 255) x0 = 255; if (x1 < 0) x1 = 0; if (x1 > 255) x1 = 255;
    if (y0 < 16) y0 = 16; if (y0 > 172) y0 = 172; if (y1 < 16) y1 = 16; if (y1 > 172) y1 = 172;
    if (y0 > y1) { s16 tx_ = x0, ty_ = y0; x0 = x1; y0 = y1; x1 = tx_; y1 = ty_; }
    if (y0 < yw && y1 >= yw) {
        s16 ax = x0, ay = y0, bx = x1, by = y1, xm;
        u8 i;
        for (i = 0; i < 8; i++) {                             /* 継ぎ目の行での x を二分で(割り算はライブラリが RAM の外へ置かれる) */
            s16 mx = (s16)((ax + bx) >> 1), my = (s16)((ay + by) >> 1);
            if (my < yw) { ax = mx; ay = my; } else { bx = mx; by = my; }
        }
        xm = bx;
        if (yw - 1 >= y0) seg(x0, y0, xm, (s16)(yw - 1), col);
        seg(xm, yw, x1, y1, col);
    } else seg(x0, y0, x1, y1, col);
}

/* ======== 第 2 形態: 原器(X 字断面の棒) ======== */
static u8  ang;
static s8  rcs, rsn;
static s16 rlen;
static void rod_update(void) {
    s16 px, py, along, cross;
    if (!(t & 3)) ang++;
    rcs = g_sin64[(u8)(ang + 16) & 63]; rsn = g_sin64[ang & 63];
    rlen = (s16)(40 + ((g_sin64[(u8)(t >> 2) & 63] + 128) >> 2) + ((g_sin64[(u8)(t >> 2) & 63] + 128) >> 3));   /* 40..136: 遠ざかり、近づく */
    px = (s16)((PX(g_pl.x) + 8 - CX) >> 1); py = (s16)((PX(g_pl.y) + 8 - CY) >> 1);
    along = (s16)(px * rcs + py * rsn); cross = (s16)(px * rsn - py * rcs);
    if (g_pl.alive && along > -(rlen >> 1) * 127 && along < (rlen >> 1) * 127 && cross > -3 * 127 && cross < 3 * 127) player_hit();
    if (!(t & 31)) {                                          /* 両端の目盛りから */
        s16 ex = (s16)((rlen * rcs) >> 7), ey = (s16)((rlen * rsn) >> 7);
        enemy_shoot(FX(CX + ex), FX(CY + ey)); enemy_shoot(FX(CX - ex), FX(CY - ey));
    }
    g_boss_aim = CY;
    if (shot_rect(CX - 9, CY - 9, 18, 18)) {
        if (g_boss_hp) g_boss_hp--;
        if (!g_boss_hp) { g_boss_state = BS_DYING; b_t = 0; score_add(5000); }
    }
}
static s16 rod_ex, rod_ey;          /* 描いてある棒の向き(変わったときだけ描き直す) */
static void rod_vram(void) {
    s16 ex, ey, qx, qy;
    u8 k;
    if ((t & 1) || gq_free() < 20) return;
    ex = (s16)((rlen * rcs) >> 7); ey = (s16)((rlen * rsn) >> 7);
    /* ★向きと長さは 4 フレームに 1 回しか変わらない。前は 2 フレームごとに同じ線を消して描き直し、LINE のコマンドが
       スプライトと重なって m6 で遅れた */
    if (ln && g_boss_state == BS_FIGHT && ex == rod_ex && ey == rod_ey) return;
    if ((u16)(sys_timer() - g_frame_t0) > 150u * 16u) return;   /* ★残りが少なければ次のフレームへ(線 10 本で約 2ms。m6 で遅れた) */
    line_erase();
    if (g_boss_state != BS_FIGHT) return;
    rod_ex = ex; rod_ey = ey;
    qx = (s16)(-rsn >> 4); qy = (s16)(rcs >> 4);              /* 横の向き × 8 */
    for (k = 0; k < 3; k++) {
        s8 o = (s8)(k - 1);
        s16 ox = (rcs < 0 ? -rcs : rcs) > (rsn < 0 ? -rsn : rsn) ? 0 : o, oy = ox ? 0 : o;
        line_add(CX - ex + ox, CY - ey + oy, CX + ex + ox, CY + ey + oy, 15);
    }
    line_add(CX + ex - qx, CY + ey - qy, CX + ex + qx, CY + ey + qy, 15);   /* 両端の十字(断面) */
    line_add(CX - ex - qx, CY - ey - qy, CX - ex + qx, CY - ey + qy, 15);
    ring_cmd(0, 0, CX - 6, CY - 6, 12, 12, 1, 0xDD);
}

/* ======== 第 3 形態: 絶対尺度(目盛りのリング) ======== */
#define NT 12
static s16 rr;                     /* 半径(× 4) */
static u8  ra, msg;
static u8  tt_hp[NT];
static const char *const msgs[4] = { "MEASURING...", "VALUE: 9999999", "VALUE OUT OF RANGE", "~" };   /* ~ は ∞(font.c) */
static s16 tpx[NT], tpy[NT];       /* 砲台の位置(1 フレームに 1 回求める) */
static u8  pos_ra;
static s16 pos_r;                  /* ring_pos で求めたときの角度と半径(変わったときだけ求め直す) */
static void ring_pos(void) {
    u8 k;
    s16 r = rr >> 2;
    pos_ra = ra; pos_r = r;
    for (k = 0; k < NT; k++) {
        tpx[k] = (s16)(CX + ((g_sin64[(u8)(ra + k * 5 + 16) & 63] * r) >> 7));
        tpy[k] = (s16)(CY + ((g_sin64[(u8)(ra + k * 5) & 63] * r) >> 7));
    }
}
static u8 msg_drawn, msg_row;
static char msg_buf[19];
static void ring_msg(u8 m) { msg = m; }
/* 判定文は画面の上(天井の下の 14 行目)に BG の文字で(画面 = リングのページ 0)。18 文字に空白で詰めて、前の文を
   上書きで消す。1 フレーム 2 行ずつ(★まとめて書くと 1 回 11ms で遅れた) */
static void msg_vram(void) {
    u8 ry = (u8)(14 + g_vscroll);
    if (msg == 0xFF) return;
    if (msg != msg_drawn) {
        const char *s = msgs[msg];
        u8 n = 0, i, o;
        while (s[n]) n++;
        o = (u8)((18 - n) >> 1);
        for (i = 0; i < 18; i++) msg_buf[i] = (char)(i >= o && i < o + n ? s[i - o] : ' ');
        msg_buf[18] = 0;
        msg_drawn = msg; msg_row = 0;
    }
    if (msg_row < 8) {
        vdp_cmd_wait();
        font_text_row(0, 56, ry, msg >= 2 ? 13 : 15, 1, msg_buf, msg_row);
        msg_row++;
    }
}
static void ring_update(void) {
    u8 k, left = 0;
    if (!(t & 7)) ra++;
    if (rr > 48 * 4) rr--;                                    /* 縮む(測り取ろうとする) */
    if (ra != pos_ra || (rr >> 2) != pos_r) ring_pos();      /* ★位置は角度か半径が変わったときだけ(掛け算 24 回。角度は 8 フレーム、
                                                                 半径は 4 フレームに 1 回変わる。前は 2 フレームに 1 回求め、m7 の遅れが固まった) */
    g_boss_aim = -1;
    for (k = 0; k < NT; k++) {
        s16 x = tpx[k], y = tpy[k];
        if (!tt_hp[k]) continue;
        left++;
        if (g_boss_aim < 0) g_boss_aim = y;
        if (shot_rect(x - 8, y - 8, 16, 16) && --tt_hp[k] == 0) fx_spawn(FX(x), FX(y));
        if (((t + k * 11) & 127) == 0) enemy_bullet(FX(x), FX(y), (s16)((CX - x) >> 2), (s16)((CY - y) >> 2));   /* 内へ */
    }
    g_boss_hp = (u8)(left * 3 + 1);
    if (left <= 8) ring_msg(1);
    if (!left) { ring_msg(2); g_boss_aim = CY; }
    if (!(t & 511)) item_spawn(FX(CX), FX(CY), 1);            /* Infinity Shard(∞ゲージを合わせる余裕) */
    if (g_boss_dmg && !left) {                                /* とどめは LIMIT BREAK だけ */
        g_boss_dmg = 0; ring_msg(3);
        g_boss_state = BS_DYING; b_t = 0; score_add(20000);
    }
    g_boss_dmg = 0;
}
static void ring_draw(void) {
    u8 k;
    for (k = 0; k < NT; k++) if (tt_hp[k]) spr_put(tpx[k] - 8, tpy[k] - 8, PAT_DIG, G_ENEMY, 0);
}
/* リングの 12 角形(砲台を順に結ぶ。指示書 5.8 の目盛りの環)。16 フレームに 1 回、消す(1..4 フレーム目に 4 分の 1 ずつ)と
   描く(5..10 フレーム目に 2 辺ずつ)を分ける。★12 角形を 4 フレームごとに消して描くと LINE の命令 1 つ 0.3ms × 28 本で毎フレーム
   遅れたので、周期を延ばした。★前は 1 フレーム目に全部を消し、3 フレームで 4 辺ずつ描いていたが、そのフレームがスプライトや
   PCM と重なって m7 の遅れが固まったので、さらに細かく分けた */
static u8 le_k, le_n;              /* 消す線の次の番号 / 1 フレームに消す本数 */
static void ring_vram(void) {
    u8 k, e;
    u8 ph = (u8)(t & 15), k0;
    if (ph < 4) {
        if (ph == 0) { le_k = 0; le_n = (u8)((ln + 3) >> 2); }
        for (e = le_n; e && le_k < ln; e--, le_k++) gq_line((u16)lx[le_k], (u8)(ly[le_k] + g_vscroll), ldx[le_k], ldy[le_k], 1);
        if (ph == 3) ln = 0;
        return;
    }
    if (ph > 9 || gq_free() < 10 || g_boss_state != BS_FIGHT) return;
    k0 = (u8)((ph - 4) * 2);
    for (k = k0; k < k0 + 2; k++) {
        u8 j = k == NT - 1 ? 0 : (u8)(k + 1);
        line_add(tpx[k], tpy[k], tpx[j], tpy[j], 15);
    }
}

/* ======== 区間 ======== */
static void boss_form(u8 sec) {
    g_sec = sec; sec_t = 0; b_t = 0;
    g_boss_state = BS_FIGHT; g_boss_hp = 40;
    if (sec == SEC8_BOSS3) {
        u8 k;
        rr = 120 * 4; ra = 0; msg = 0xFF; msg_drawn = 0xFF; msg_row = 8; ring_pos();
        for (k = 0; k < NT; k++) tt_hp[k] = 3;
        ring_msg(0);
    }
}

void ovl_init(void) {
    g_ter.lo = s8_floor; g_ter.hi = s8_ceil; g_ter.lo_add = 0; g_ter.ncol = S8_COLS;   /* DROP の爆雷が這う床・天井 */
    g_boss_state = BS_NONE; t = 0; rj = 0; rph = 0; rrow = 0; fo = fi = 0; ln = 0; ang = 0;
    g_sec = SEC8_CORE; sec_t = 0; sec_wx = g_wx;
    enemy_waves(WAVES_AIR);
}

void ovl_update(void) {
    s16 c = col_of_screen();
    sec_t++;
    pulse();
    switch (g_sec) {
    case SEC8_CORE:                                           /* 原器の間で、画面がページ 0 にそろったら止める */
        if (c >= S8_COL_BOSS && (g_wx & 511u) == 0 && rj >= RJ_END) { g_scroll_speed = 0; enemy_waves(WAVES_NONE); boss_form(SEC8_BOSS1); }
        break;
    case SEC8_BOSS1: case SEC8_BOSS2: case SEC8_BOSS3:
        if (g_boss_state == BS_DYING) {
            if (!(b_t & 3)) fx_spawn(FX(CX - 40 + (rnd8() & 79)), FX(CY - 40 + (rnd8() & 79)));
            if (++b_t >= 120) {
                if (g_sec == SEC8_BOSS1) { ring_cmd(0, 0, CX - 48, CY - 48, 96, 96, 1, 0x11); boss_form(SEC8_BOSS2); }
                else if (g_sec == SEC8_BOSS2) boss_form(SEC8_BOSS3);
                else {                                       /* ★判定文(リングの BG の文字)を消す。消さずにいると脱出の始めに
                                                                   「FINITY」が残って流れた */
                    ring_cmd(0, 0, 56, 14, 144, 8, 1, 0x11);
                    g_boss_state = BS_NONE; g_sec = SEC8_ESCAPE; sec_t = 0; sec_wx = g_wx; stage_banner("ESCAPE");
                }
            }
        }
        break;
    case SEC8_ESCAPE:                                         /* 最高速で脱出。全部の帯で破片が降る */
        if (g_scroll_speed < 32 && !(sec_t & 3)) g_scroll_speed++;
        if (!(sec_t & 3)) enemy_bullet(FX(rnd8()), FX(8), -16, (s16)(16 + (rnd8() & 15)));
        if ((u16)(g_wx - sec_wx) >= ESC_LEN) { g_sec = SEC8_OUT; sec_t = 0; g_hall = 0; stage_sky_redraw(); }   /* 縦穴の壁を星空に */
        break;
    case SEC8_OUT:                                            /* 宇宙へ抜ける → 終幕 */
        if (g_scroll_speed > 4 && !(sec_t & 3)) g_scroll_speed--;
        if (g_scam_q > -192 * 16) g_scam_q -= 24;
        if (sec_t == 200) { ovl_request(OVL_ENDING); g_sec = SEC8_OUT + 1; return; }
        break;
    default:
        break;
    }
    {   /* 当たり(回廊の天井と床) */
        u16 c0 = (u16)(((u16)(g_wx + (u16)PX(g_pl.x) - (g_w0 << 3))) >> 3), cc;
        s16 lo = 0, hi = PLAY_H - 16;
        if (g_sec != SEC8_OUT) for (cc = c0; cc <= (u16)(c0 + 2); cc++) {
            u8 k = cc < S8_COLS ? g_map[cc] : 16;
            if ((s16)(s8_ceil[k] - g_scam) > lo) lo = (s16)(s8_ceil[k] - g_scam);
            if ((s16)(s8_floor[k] - 16 - g_scam) < hi) hi = (s16)(s8_floor[k] - 16 - g_scam);
        }
        if (hi < lo) hi = lo;
        g_pl_ymin = lo < 0 ? 0 : lo; g_pl_ymax = hi;
    }
}

void ovl_logic(void) {
    t++;
    if (g_sec == SEC8_CORE) rot_update();
    if (g_boss_state != BS_FIGHT) { g_boss_dmg = 0; return; }
    shots_prepare();
    if (g_sec == SEC8_BOSS1) bell_update();
    else if (g_sec == SEC8_BOSS2) rod_update();
    else if (g_sec == SEC8_BOSS3) ring_update();
    if (g_sec != SEC8_BOSS3 && g_boss_dmg) {                  /* LIMIT BREAK(第 1・2 形態) */
        u8 d = g_boss_dmg; g_boss_dmg = 0;
        g_boss_hp = g_boss_hp > d ? (u8)(g_boss_hp - d) : 1;
    }
}
void ovl_draw(void) {
    if (g_sec == SEC8_BOSS3 && g_boss_state == BS_FIGHT) ring_draw();
    if (g_sec == SEC8_ESCAPE) {                               /* 脱出: 光の流線が後ろへ飛ぶ(最高速の手応え) */
        u8 i;
        for (i = 0; i < 4; i++) spr_put((s16)((u8)(200 - sec_t * 24 - i * 71)), (s16)(24 + ((i * 97 + (sec_t >> 3) * 31) & 127)), PAT_STREAK, G_MISC, CS_CURSOR);
    }
}
void ovl_vram(void) {
    u8 late = (u16)(sys_timer() - g_frame_t0) > 150u * 16u;   /* ★重いフレームは絵の転送・文字を次へ回す(形態の替わり目に
                                                                 重なって 1 周の遅れが増えた) */
    if (!late) rot_vram();
    if (g_sec == SEC8_BOSS1 && g_boss_state == BS_FIGHT) bell_vram();
    if (g_sec == SEC8_BOSS2) rod_vram();
    if (g_sec == SEC8_BOSS3) { ring_vram(); if (!late) msg_vram(); }
    gq_pump(0);
}
void ovl_bands(s16 sc, u16 nx) {
    (void)nx;
    stage_ev_pal((s16)(0 - sc), (g_sec == SEC8_BOSS3 && g_boss_state == BS_DYING && (b_t & 4)) ? flash : inside);
}
