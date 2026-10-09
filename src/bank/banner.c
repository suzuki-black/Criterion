/* banner.c — 面の開始表示の VRAM の仕事(第 2 バンク。状態と帯は stage.c、game.h の Banner)。
   ★帯はページ 2|3 の (行 + R#23) の行を見せる。R#23 は 252(s = -192 のときの値)に固定するので、画面の 172..187 行 =
     168..183 行。ページ 2 のそこは起動ロゴの黒い区間なので、色 1 で塗って使い、終わったら黒へ戻す。
     帯の色 1 はその高さの空の色にする(透けて見える)。
   ★短冊を使っていないとき(p3): ページ 3 も塗り、横スクロールを 256(ページ 3 = 何もない)から 0 へ寄せて文字を左から入れる。
   ★短冊を使っている間(読み込み中も)はページ 3 を塗らない(ページ 3 の 160..183 行は短冊の地面。デモの説明の帯が
     そこを塗り、以後に流れてくる 1 面の列の街路が空の色になって、Pawn の黒い余白が四角く見えた)。そのときは文字を
     ページ 2 の BAN_STG 行(帯には見えない)へ書き、HMMM で帯へ写す幅を広げて滑り込ませる(前は滑らせずにその場に
     出していた。指示書との突き合わせで直した)。 */
#include "game.h"
#include "vdp.h"
#include "sys.h"
#include "font.h"

extern u16 g_frame_t0;            /* main.c: このフレームの始まり */

void banner_vram(void) {
    Banner *b = &g_ban;
    u8 t = b->t;
    if (t <= 3 && vdp_cmd_busy()) return;                     /* 塗りは VDP が空いてから(待つと遅れた。3 面の入り) */
    if (t == 1) { vdp_hmmv(0, 512 + BAN_ROW - 8, 256, 24, 0x11); b->cp = 0; }   /* 1 フレームに 1 つ(VDP を待たない) */
    else if (t == 2) vdp_hmmv(0, b->p3 ? 768 + BAN_ROW - 8 : 512 + BAN_STG, 256, b->p3 ? 24 : 8, 0x11);
    else if (t >= 3 && t < BAN_IN) {                          /* 文字は 1 フレームに 2 行まで(一度に書くと 120 ライン。2 面の入りで遅れた) */
        u8 y = b->p3 ? (u8)(BAN_ROW + 4) : (u8)BAN_STG;
        /* ★重いフレームは次へ。1 行(約 25 ライン)ずつ書き、2 行目は残りがあるときだけ。書いた行(cp)が足りなければ t を進めない
           (150 ラインまで 2 行を書いていたので、7 面の試験の始まりで遅れた) */
        if ((u16)(sys_timer() - g_frame_t0) > 150u * 16u) return;
        vdp_cmd_wait();
        if (b->cp < 8) font_text_row(2, b->x, y, b->fg, 1, b->text, (u8)b->cp++);
        if (b->cp < 8 && (u16)(sys_timer() - g_frame_t0) <= 120u * 16u) font_text_row(2, b->x, y, b->fg, 1, b->text, (u8)b->cp++);
        b->bx = 256;
        if (b->cp < (u16)((t - 2) << 1)) return;
    } else if (t == BAN_END) {
        vdp_hmmv(0, 512 + BAN_ROW - 8, 256, BAN_STG + 8 - (BAN_ROW - 8), 0x00);   /* 起動ロゴの黒い区間へ戻す */
        b->t = 0;
        return;
    }
    if (t >= BAN_IN && b->bx) b->bx -= (b->bx + 7) >> 3;     /* 左から寄ってきて止まる */
    if (!b->p3 && t >= BAN_IN && b->cp != (u16)(256 - b->bx) && !vdp_cmd_busy()) {
        b->cp = (u16)(256 - b->bx);                           /* 短冊を使っている面: 置き場の文字を帯へ写して寄せる */
        if (b->cp) vdp_hmmm(b->bx, 512 + BAN_STG, 0, 512 + BAN_ROW + 4, b->cp, 8);
    }
    b->t++;
}
