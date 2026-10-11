/* weapon.c — 強化の ECHO / AEGIS / DROP と THRUST の残像(指示書 2.3。第 2 バンク)。
   ・ECHO(1..4 機): 自機の過去の位置(動いたときだけ記録する軌跡)を ECHO_GAP 個おきになぞる追従機。自機が撃つと
     同じ主砲を撃つ。敵弾を 1 発吸い、その後 3 秒は点滅して吸わない。
   ・AEGIS 1: 前面に目盛りの防壁(耐久 8)。2: 自機のまわりを回る 4 枚の輪(耐久 4)。耐久が尽きると AEGIS は 0 に戻る。
   ・DROP 1: 撃っている間、下へ爆雷を落とす。床(g_ter)に着くと前へ這い、段差で落ち、壁に当たると消える。
     2: 上向きの爆雷も(天井を這う)。爆雷は自機の弾(g_pshot の on = PS_DROP_DN / UP)なので、敵・ボス・Pawn の
     当たりはふつうの弾と同じ所で取られる。
   ・THRUST: 動いている間、段階に応じて離れた位置に残像(G_MISC、暗い色の組)。
   分身と防壁は G_MISC にスプライトの段の最後で置く(溢れたら真っ先に消える = 優先順位がいちばん低い)。分身は
   1 フレームおきに半分ずつの点滅(MSX のオプションと同じ見え方)。 */
#include "game.h"
#include "spr.h"
#include "snd.h"
#include "ovl.h"
#include "sprite_data.h"
#include "bank_int.h"

#define TRAIL     64               /* 軌跡の長さ(2 のべき乗) */
#define ECHO_GAP  12               /* 分身どうしの間隔(軌跡の個数) */
#define ECHO_MAX  4
#define BLINK     180              /* 吸ったあと吸わない間(3 秒) */
#define DROP_CD   36               /* 爆雷を落とす間隔(フレーム) */

static const s8 ring_dx[64] = { 22, 22, 22, 21, 20, 19, 18, 17, 16, 14, 12, 10, 8, 6, 4, 2, 0, -2, -4, -6, -8, -10, -12, -14, -16, -17, -18, -19, -20, -21, -22, -22, -22, -22, -22, -21, -20, -19, -18, -17, -16, -14, -12, -10, -8, -6, -4, -2, 0, 2, 4, 6, 8, 10, 12, 14, 16, 17, 18, 19, 20, 21, 22, 22 };
static const s8 ring_dy[64] = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 17, 18, 19, 20, 21, 22, 22, 22, 22, 22, 21, 20, 19, 18, 17, 16, 14, 12, 10, 8, 6, 4, 2, 0, -2, -4, -6, -8, -10, -12, -14, -16, -17, -18, -19, -20, -21, -22, -22, -22, -22, -22, -21, -20, -19, -18, -17, -16, -14, -12, -10, -8, -6, -4, -2 };   /* AEGIS 2 の輪(半径 22)。★掛け算を毎フレームしない */
static u8 tr_x[TRAIL], tr_y[TRAIL], tr_h, last_x, last_y;
static u8 blink[ECHO_MAX];
static u8 aeg_lv, aeg_dur, rot, drop_cd, moved, echo_prev, vol;   /* vol: 撃った回数(分身が交互に撃つ) */
#define drops g_drops                /* 生きている爆雷の数(本体の変数: 強化も爆雷も無ければ本体は weap_* を呼ばない) */

/* ---- 主砲(自機と分身。player.c は呼び口から) ---- */
static s16 sx0, sy0;                                          /* 撃つ位置 */
static Bullet *shoot(s16 vy, u8 pierce, s16 dy) {
    /* ★空きは前の続きから探し、満杯ならそのフレームはもう探さない(ECHO の分身 4 機 × 5WAY が弾の枠を毎回
       頭から探して、撃つフレームが遅れた) */
    Bullet *b = &g_pshot[g_shot_at];
    for (; g_shot_at < PS_SHOTS; g_shot_at++, b++) {
        if (b->on) continue;
        b->on = 1; b->pierce = pierce;
        b->x = sx0 + FX(12); b->y = sy0 + FX(8) + dy;         /* 弾の座標は中心 */
        b->vx = FX(8); b->vy = vy;
        return b;
    }
    return 0;
}

void pl_fire_from(s16 x, s16 y) {
    sx0 = x; sy0 = y;
    if (g_gauge.ray) {
        Bullet *c = shoot(0, 1, 0);
        if (g_gauge.ray >= 2) {                               /* 太さ倍(3 本を束ねる)。★3 本そろったら真ん中を 3、横を 2 にして
                                                                 1 枚(PAT_RAY3)で描く(shots_draw。pierce はどこでも真偽で見る) */
            Bullet *u = shoot(0, 1, FX(-4)), *d = shoot(0, 1, FX(4));
            if (c && u && d) { c->pierce = 3; u->pierce = d->pierce = 2; }
        }
    } else {
        shoot(0, 0, 0);
        if (g_gauge.split >= 1) { shoot(FX(-1) - 8, 0, 0); shoot(FX(1) + 8, 0, 0); }
        if (g_gauge.split >= 2) { shoot(FX(-3), 0, 0); shoot(FX(3), 0, 0); }
    }
}

#define SHOT_ROW_MAX 3
#define G_SHOT_N 2                   /* asm 用(spr.h の G_SHOT) */
/* ★自機弾は 1 ラインに SHOT_ROW_MAX 枚まで(測定: 8 枚を超えた行の約半分が自機弾で、同じ高さの敵・敵弾が消えた)。
   8 行ごとの区切り(sh_rows)で数え、1 枚が掛かる 3 つの区切りのどこかが満ちていればそのフレームは描かない。描き始めを
   フレームごとにずらすので、溢れた弾は消えずにちらつく。pierce: 0 = 弾、1 = RAY、2 = 束ねた横(描かない)、3 = 束ねた真ん中(PAT_RAY3)。
   ★asm(C で書いた版は 1 フレームに約 9 ライン使い、PCM の鳴る場面の遅れを増やした)。IX = 弾、C = 絵、B = x から引く量 */
static u8 sh_rows[30], sh_rot;     /* ★第 2 バンクの変数は 0 から始まらない(sh_rot は押さえる) */
void shots_draw(void) __naked {
    __asm
        ld   a, (_sh_rot)
        cp   #PS_SHOTS
        jr   c, 00001$
        xor  a
    00001$:
        ld   c, a                  ; C = 描き始め
        inc  a
        cp   #PS_SHOTS
        jr   c, 00002$
        xor  a
    00002$:
        ld   (_sh_rot), a
        ld   hl, #_sh_rows
        ld   b, #30
        xor  a
    00003$:
        ld   (hl), a
        inc  hl
        djnz 00003$
        push ix
        ld   a, (_g_drops)         ; 爆雷を先に(後から置くと毎回溢れた)
        or   a
        jr   z, 00010$
        push bc
        ld   ix, #_g_pshot + PS_SHOTS * 10
        ld   b, #MAX_PSHOT - PS_SHOTS
    00004$:
        push bc
        ld   a, 8 (ix)
        or   a
        ld   c, #PAT_DROP
        ld   b, #8
        call nz, 00050$
        ld   de, #10
        add  ix, de
        pop  bc
        djnz 00004$
        pop  bc
    00010$:
        ld   l, c                  ; IX = g_pshot + C * 10
        ld   h, #0
        add  hl, hl
        ld   d, h
        ld   e, l
        add  hl, hl
        add  hl, hl
        add  hl, de
        ld   de, #_g_pshot
        add  hl, de
        push hl
        pop  ix
        ld   b, #PS_SHOTS
    00011$:
        push bc
        ld   a, 8 (ix)
        or   a
        jr   z, 00014$
        ld   a, 9 (ix)             ; pierce
        ld   c, #PAT_SHOT
        or   a
        jr   z, 00013$
        ld   c, #PAT_RAY
        dec  a
        jr   z, 00013$
        dec  a
        jr   z, 00014$             ; 束ねた横: 描かない
        ld   c, #PAT_RAY3
    00013$:
        ld   b, #2
        call 00050$
    00014$:
        pop  bc
        ld   de, #10
        add  ix, de
        inc  c
        ld   a, c
        cp   #PS_SHOTS
        jr   nz, 00015$
        ld   c, #0
        ld   ix, #_g_pshot
    00015$:
        djnz 00011$
        pop  ix
        ret
        ; ---- 弾 IX を絵 C で置く(x は PX(x) - B、y は PX(y) - 8)。区切りが満ちていれば置かない ----
    00050$:
        ld   l, 2 (ix)
        ld   h, 3 (ix)
        sra  h
        rr   l
        sra  h
        rr   l
        sra  h
        rr   l
        sra  h
        rr   l                     ; HL = PX(y) = 上端 + 8
        ld   a, h
        or   a
        ret  nz                    ; 画面の外
        ld   a, l
        cp   #216
        ret  nc
        push hl
        rrca
        rrca
        rrca
        and  #31
        ld   hl, #_sh_rows
        add  a, l
        ld   l, a
        jr   nc, 00051$
        inc  h
    00051$:
        ld   a, (hl)
        cp   #SHOT_ROW_MAX
        jr   nc, 00059$
        inc  hl
        ld   a, (hl)
        cp   #SHOT_ROW_MAX
        jr   nc, 00059$
        inc  hl
        ld   a, (hl)
        cp   #SHOT_ROW_MAX
        jr   nc, 00059$
        inc  (hl)
        dec  hl
        inc  (hl)
        dec  hl
        inc  (hl)
        pop  hl
        ld   de, #-8
        add  hl, de
        ex   de, hl                ; DE = 上端
        ld   l, 0 (ix)
        ld   h, 1 (ix)
        sra  h
        rr   l
        sra  h
        rr   l
        sra  h
        rr   l
        sra  h
        rr   l
        ld   a, l
        sub  b
        ld   l, a
        jr   nc, 00052$
        dec  h
    00052$:
        xor  a                     ; spr_put(x, y, 絵, G_SHOT, 0): 引数は 1 バイトずつ cs → group → pat の順に積む
        push af
        inc  sp
        ld   a, #G_SHOT_N
        push af
        inc  sp
        ld   a, c
        push af
        inc  sp
        call _spr_put              ; spr_put が 3 バイト下ろして戻る
        ret
    00059$:
        pop  hl
        ret
    __endasm;
}

/* Scanner の測定線(3・7 面): いちばん高い強化を 1 段奪う */
void gauge_steal(void) {
    u8 *v = &g_gauge.thrust, *m = v, i;
    for (i = 1; i < 6; i++) if (v[i] > *m) m = v + i;
    if (*m) { (*m)--; g_gauge.flash = 16; sfx(SFX_DENY); }
}

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
    if (g_fired) vol++;
    n = g_gauge.echo;
    for (i = 0; i < n; i++) {
        u8 k = echo_at(i);
        s16 ex = tr_x[k], ey = tr_y[k];
        if (g_fired && ((i ^ vol) & 1)) pl_fire_from(FX(ex), FX(ey));   /* ★分身は 1 回おきに交互に撃つ(弾の枠はどのみち満杯。重かった) */
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
            u8 a = (u8)(((u8)(rot + (i << 6))) >> 2);
            target((s16)(px + 1 + ring_dx[a]), (s16)(py + 1 + ring_dy[a]), 14, 14, ECHO_MAX);
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

/* ★分身と防壁は G_MISC(敵の枠を回していた間は、中ボスの部品や敵が消えた。指摘)。G_MISC の 4 枠を超えた分は、spr.c の窓が
   フレームごとに顔ぶれをずらしてちらつかせる(前は分身を 1 フレームおきに半分ずつ描き、1 ラインの制限と重なって消えて見えた) */
static void echo_draw(s16 dy) {
    u8 i, n = g_gauge.echo;
    for (i = 0; i < n; i++) {
        u8 k = echo_at(i);
        if (!(blink[i] & 4)) spr_put(tr_x[k], (s16)(tr_y[k] + dy), PAT_ECHO, G_MISC, CS_EXPL);
    }
}

static void aegis_draw(u8 px, u8 py, s16 dy) {
    u8 i;
    if (!aeg_lv || (aeg_dur <= 2 && (rot & 8))) return;       /* 耐久が残り少ないと点滅 */
    if (aeg_lv == 1) {
        spr_put(px + 14, (s16)(py - 8 + dy), PAT_AEGIS, G_MISC, CS_CURSOR);
        spr_put(px + 14, (s16)(py + 8 + dy), PAT_AEGIS, G_MISC, CS_CURSOR);
    } else for (i = 0; i < 4; i++) {
        u8 a = (u8)(((u8)(rot + (i << 6))) >> 2);
        spr_put((s16)(px + ring_dx[a]), (s16)(py + ring_dy[a] + dy), PAT_AEGIS, G_MISC, CS_CURSOR);
    }
}

void weap_draw(void) {
    u8 px, py;
    s16 dy;
    if (!g_pl.alive || (g_pl.invul & 4)) return;
    px = (u8)PX(g_pl.x); py = (u8)PX(g_pl.y); dy = g_pl_dy;
    echo_draw(dy); aegis_draw(px, py, dy);
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

/* LIMIT BREAK の衝撃波(指示書 2.3「∞の字の衝撃波が拡大回転しながら画面を一掃」): 発動から 24 フレーム、画面の中央から
   結晶 8 つの輪が回りながら広がる。敵弾は消えているので敵弾の枠を使う。∞ の紋章は infx(確定の演出)が中央へ飛ぶ */
void lb_draw(void) {
    u8 i, r = (u8)((24 - g_lb) * 7), a0 = (u8)(g_lb * 12);
    for (i = 0; i < 8; i++) {
        u8 a = (u8)(a0 + i * 32);
        s16 dx = (s16)(((s16)g_sin64[((u8)(a + 64) >> 2) & 63] * r) >> 7), dy = (s16)(((s16)g_sin64[a >> 2] * r) >> 7);
        spr_put((s16)(120 + dx), (s16)(86 + dy), PAT_SHARD, G_EBUL, 0);
    }
}
