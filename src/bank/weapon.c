/* weapon.c — 強化の ECHO / AEGIS / DROP と THRUST の残像(指示書 2.3。第 2 バンク)。
   ・ECHO(1..4 機): 自機の過去の位置(動いたときだけ記録する軌跡)を ECHO_GAP 個おきになぞる追従機。自機が撃つと
     同じ主砲を撃つ。敵弾を 1 発吸い、その後 3 秒は点滅して吸わない。
   ・AEGIS 1: 前面に目盛りの防壁(耐久 8)。2: 自機のまわりを回る 4 枚の輪(耐久 4)。耐久が尽きると AEGIS は 0 に戻る。
   ・DROP 1: 撃っている間、下へ爆雷を落とす。床(g_ter)に着くと前へ這い、段差で落ち、壁に当たると消える。
     2: 上向きの爆雷も(天井を這う)。爆雷は自機の弾(g_pshot の on = PS_DROP_DN / UP)なので、敵・ボス・Pawn の
     当たりはふつうの弾と同じ所で取られる。
   ・THRUST: 動いている間、段階に応じて離れた位置に残像(G_MISC、暗い色の組)。
   分身と防壁は G_OPT(強化を持っている間だけ 4 枠。main.c が割り振りに 8 を足す)。溢れた分はグループの中で順番を
   回すので、全部そろうと点滅して見える(MSX のオプションと同じ)。 */
#include "game.h"
#include "spr.h"
#include "snd.h"
#include "ovl.h"
#include "sprite_data.h"

#define TRAIL     64               /* 軌跡の長さ(2 のべき乗) */
#define ECHO_GAP  12               /* 分身どうしの間隔(軌跡の個数) */
#define ECHO_MAX  4
#define BLINK     180              /* 吸ったあと吸わない間(3 秒) */
#define RING_R    22               /* AEGIS 2 の輪の半径 */
#define DROP_CD   36               /* 爆雷を落とす間隔(フレーム) */
#define TER_NONE  0x7FFF

static u8 tr_x[TRAIL], tr_y[TRAIL], tr_h, last_x, last_y;
static u8 blink[ECHO_MAX];
static u8 aeg_lv, aeg_dur, rot, drop_cd, moved, echo_prev;
#define drops g_drops                /* 生きている爆雷の数(本体の変数: 強化も爆雷も無ければ本体は weap_* を呼ばない) */

void weap_init(void) {
    u8 i, x = (u8)PX(g_pl.x), y = (u8)PX(g_pl.y);
    for (i = 0; i < TRAIL; i++) { tr_x[i] = x; tr_y[i] = y; }
    tr_h = 0; last_x = x; last_y = y; moved = 0;
    for (i = 0; i < ECHO_MAX; i++) blink[i] = 0;
    aeg_lv = 0; aeg_dur = 0; rot = 0; drop_cd = 0; drops = 0; echo_prev = 0;
}

static u8 echo_at(u8 i) { return (u8)((tr_h - (u8)((i + 1) * ECHO_GAP)) & (TRAIL - 1)); }

/* 敵弾を吸う的: 左上 (x, y)・幅 w・高さ h(画面のドット、8 ビット)。who = 分身の番号 / AEGIS は ECHO_MAX */
#define NT (ECHO_MAX + 4)
static u8 tx[NT], ty[NT], tw[NT], th[NT], tw_who[NT], nt, got[ECHO_MAX + 1];
static void target(s16 x, s16 y, u8 w, u8 h, u8 who) {
    if (x < -8 || x > 255 || y < -8 || y > PLAY_H) return;
    tx[nt] = (u8)x; ty[nt] = (u8)y; tw[nt] = w; th[nt] = h; tw_who[nt] = who; nt++;
}
/* ★敵弾を 1 回だけ見て、的ごとに比べる(的ごとに敵弾を全部見ると、分身 4 機 + 輪 4 枚で 1 フレームに 128 回 16 ビットの
   変換をしていた)。AEGIS は 1 フレームに 4 発まで、分身は 1 発ずつ */
static void absorb_all(void) {
    u8 i, k;
    Bullet *e = g_eshot;
    for (k = 0; k <= ECHO_MAX; k++) got[k] = 0;
    if (!nt) return;
    for (i = 0; i < MAX_ESHOT; i++, e++) {
        s16 x, y;
        u8 bx, by;
        if (!e->on) continue;
        x = PX(e->x); y = PX(e->y);
        if (x < 0 || x > 255 || y < 0 || y > PLAY_H) continue;
        bx = (u8)x; by = (u8)y;
        for (k = 0; k < nt; k++) {
            u8 w = tw_who[k];
            if ((u8)(bx - tx[k]) >= tw[k] || (u8)(by - ty[k]) >= th[k]) continue;
            if (got[w] >= (w == ECHO_MAX ? 4 : 1)) continue;
            got[w]++; e->on = 0;
            break;
        }
    }
}

/* 画面の x の床(up = 0)・天井(up = 1)の画面の y。地形が無ければ TER_NONE */
static s16 ter_y(s16 sx, u8 up) {
    u16 c;
    u8 k;
    if (g_ter.flat) return up ? TER_NONE : (s16)(g_ter.flat - g_scam);
    if (!g_ter.lo || sx < 0 || sx > 255) return TER_NONE;
    c = (u16)(((u16)(g_wx + (u16)sx) >> 3) - g_w0);
    if (c >= g_ter.ncol) return TER_NONE;
    k = g_map[c];
    k = up ? (g_ter.hi ? g_ter.hi[k] : 0) : g_ter.lo[k];
    if (!k) return TER_NONE;                                  /* 表の 0 = その列に床(天井)が無い(5 面の空の列など) */
    return (s16)(k + (up ? 0 : g_ter.lo_add) - g_scam);
}

static void drop_spawn(u8 kind) {
    u8 i, n = 0;
    Bullet *b, *f = 0;
    for (i = PS_SHOTS, b = g_pshot + PS_SHOTS; i < MAX_PSHOT; i++, b++) {   /* 爆雷専用の枠 */
        if (b->on == kind) n++;
        else if (!b->on && !f) f = b;
    }
    if (n >= 2 || !f) return;                                 /* 1 方向に 2 発まで */
    f->on = kind; f->pierce = 0; f->vx = 0;
    f->x = g_pl.x + FX(8);
    f->y = g_pl.y + (kind == PS_DROP_DN ? FX(12) : FX(4));
    f->vy = kind == PS_DROP_DN ? FX(1) : -FX(1);
    drops++;
}

/* 爆雷: 地形が無ければ落ちて(上向きは昇って)消える。床に着いたら前へ這い、段差で落ち、壁で消える */
static void drop_update(void) {
    u8 i, n = 0;
    Bullet *b;
    if (!drops) return;                                       /* ★爆雷が無いフレームに 20 枠を見ると 0.27ms かかった */
    for (i = PS_SHOTS, b = g_pshot + PS_SHOTS; i < MAX_PSHOT; i++, b++) {
        s16 x, y, f, f2, c;
        u8 up;
        if (b->on < PS_DROP_DN) continue;
        up = b->on == PS_DROP_UP;
        x = PX(b->x); y = PX(b->y);
        f = ter_y(x, up);
        c = up ? (s16)(y - 4) : (s16)(y + 4);                 /* 爆雷の床(天井)に触れる所 */
        if (f == TER_NONE || (up ? c > f : c < f)) {          /* 落ちる */
            if (up) { if (b->vy > -FX(3)) b->vy -= 3; }
            else if (b->vy < FX(3)) b->vy += 3;
            b->y += b->vy;
            b->x += 8;                                        /* 前へ少し流れる */
            if (f != TER_NONE) {
                c = up ? (s16)(PX(b->y) - 4) : (s16)(PX(b->y) + 4);
                if (up ? c <= f : c >= f) { b->y = FX(up ? f + 4 : f - 4); b->vy = 0; }
            }
        } else {                                              /* 這う */
            f2 = ter_y((s16)(x + 6), up);
            if (f2 != TER_NONE && (up ? f2 > f + 4 : f2 < f - 4)) { fx_spawn(b->x, b->y); b->on = 0; continue; }   /* 壁 */
            b->x += FX(1) + 8;
            b->y = FX(up ? f + 4 : f - 4);
        }
        y = PX(b->y);
        if (PX(b->x) > 255 || y < -8 || y > PLAY_H + 8) b->on = 0;
        else n++;
    }
    drops = n;                                                /* 当たって消えた分(敵・ボス・壁)もここで数え直す */
}

void weap_update(void) {
    u8 i, n, px, py;
    if (!g_slow) drop_update();                               /* ★処理落ちのフレームは弾と同じく止める(当たりを取らない) */
    if (!g_pl.alive) return;
    px = (u8)PX(g_pl.x); py = (u8)PX(g_pl.y);
    if (!echo_prev && g_gauge.echo) {                         /* 分身が出たとき: 軌跡を今の位置で埋める(持っていない間は記録しない) */
        u8 k;
        for (k = 0; k < TRAIL; k++) { tr_x[k] = px; tr_y[k] = py; }
        last_x = px; last_y = py;
    }
    echo_prev = g_gauge.echo;
    moved = px != last_x || py != last_y;
    if (moved) { tr_h = (u8)((tr_h + 1) & (TRAIL - 1)); tr_x[tr_h] = px; tr_y[tr_h] = py; last_x = px; last_y = py; }
    rot += 3;
    /* DROP(★分身の弾より先に: 分身 4 機が撃つと自機の弾の枠が埋まり、爆雷が出せなかった) */
    if (drop_cd) drop_cd--;
    if (g_gauge.drop && g_fired && !drop_cd) {
        drop_spawn(PS_DROP_DN);
        if (g_gauge.drop >= 2) drop_spawn(PS_DROP_UP);
        drop_cd = DROP_CD;
    }
    /* ECHO */
    nt = 0;
    n = g_gauge.echo;
    for (i = 0; i < n; i++) {
        u8 k = echo_at(i);
        s16 ex = tr_x[k], ey = tr_y[k];
        if (g_fired) pl_fire_from(FX(ex), FX(ey));
        if (blink[i]) blink[i]--;
        else target((s16)(ex + 1), (s16)(ey + 1), 14, 14, i);
    }
    /* AEGIS: 段階が上がったら耐久を入れ直す(被弾・LIMIT BREAK で下がったら合わせるだけ) */
    if (g_gauge.aegis != aeg_lv) {
        if (g_gauge.aegis > aeg_lv) aeg_dur = g_gauge.aegis == 1 ? 8 : 4;
        aeg_lv = g_gauge.aegis;
    }
    if (aeg_lv == 1) target((s16)(px + 18), (s16)(py - 10), 12, 36, ECHO_MAX);
    else if (aeg_lv >= 2) {
        for (i = 0; i < 4; i++) {
            u8 a = (u8)(rot + (i << 6));
            s16 dx = (s16)(((s16)g_sin64[(u8)(a + 64) >> 2 & 63] * RING_R) >> 7), dy = (s16)(((s16)g_sin64[a >> 2] * RING_R) >> 7);
            target((s16)(px + 8 + dx - 7), (s16)(py + 8 + dy - 7), 14, 14, ECHO_MAX);
        }
    }
    if (rot & 2) absorb_all();                                /* ★2 フレームに 1 回(敵弾は 1 フレームに 3 ドットまで、的は 14 ドット。
                                                                 毎フレームだと分身 4 機で 0.34ms) */
    else { u8 k; for (k = 0; k <= ECHO_MAX; k++) got[k] = 0; }
    for (i = 0; i < n; i++) if (got[i]) { blink[i] = BLINK; sfx(SFX_HIT); }
    n = got[ECHO_MAX];
    if (n) {
        sfx(SFX_HIT);
        if (aeg_dur > n) aeg_dur -= n;
        else { aeg_dur = 0; g_gauge.aegis = 0; aeg_lv = 0; fx_spawn(FX(px + 20), FX(py + 8)); }   /* 耐久が尽きた */
    }
}

static void echo_draw(s16 dy) {
    u8 i, n = g_gauge.echo;
    for (i = 0; i < n; i++) {
        u8 k = echo_at(i);
        if (!(blink[i] & 4)) spr_put(tr_x[k], (s16)(tr_y[k] + dy), PAT_ECHO, G_OPT, 0);
    }
}

static void aegis_draw(u8 px, u8 py, s16 dy) {
    u8 i;
    if (!aeg_lv || (aeg_dur <= 2 && (rot & 8))) return;       /* 耐久が残り少ないと点滅 */
    if (aeg_lv == 1) {
        spr_put(px + 14, (s16)(py - 8 + dy), PAT_AEGIS, G_OPT, 0);
        spr_put(px + 14, (s16)(py + 8 + dy), PAT_AEGIS, G_OPT, 0);
    } else for (i = 0; i < 4; i++) {
        u8 a = (u8)(rot + (i << 6));
        s16 ox = (s16)(((s16)g_sin64[(u8)(a + 64) >> 2 & 63] * RING_R) >> 7), oy = (s16)(((s16)g_sin64[a >> 2] * RING_R) >> 7);
        spr_put((s16)(px + ox), (s16)(py + oy + dy), PAT_AEGIS, G_OPT, 0);
    }
}

void weap_draw(void) {
    u8 px, py;
    s16 dy;
    if (!g_pl.alive || (g_pl.invul & 4)) return;
    px = (u8)PX(g_pl.x); py = (u8)PX(g_pl.y); dy = g_pl_dy;
    /* ★分身 4 機と防壁で G_OPT の 4 枠を超えると、後から置いたものが毎フレーム溢れて見えなくなる。順番をフレームごとに
       入れ替えて、ちらつきに散らす */
    if (rot & 4) { aegis_draw(px, py, dy); echo_draw(dy); }
    else { echo_draw(dy); aegis_draw(px, py, dy); }
    /* THRUST の残像: 動いている間、段階が上がるほど後ろへ伸びる(3 段から 2 つ) */
    if (g_gauge.thrust && moved && !(g_ovl == OVL_S3 && g_scroll_speed >= 16)) {   /* 3 面の高速は面が残像を出す */
        u8 k = (u8)((tr_h - (u8)(g_gauge.thrust * 2)) & (TRAIL - 1));
        spr_put(tr_x[k], (s16)(tr_y[k] + dy), PAT_SHIP_A, G_MISC, CS_DIM);
        if (g_gauge.thrust >= 3) {
            k = (u8)((tr_h - (u8)(g_gauge.thrust * 4)) & (TRAIL - 1));
            spr_put(tr_x[k], (s16)(tr_y[k] + dy), PAT_SHIP_A, G_MISC, CS_DIM);
        }
    }
}
