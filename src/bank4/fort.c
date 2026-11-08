/* fort.c — 5 面の出口: 雲海の向こうに 6 面の日時計の要塞が見えてくる(第 4 バンク。2026-11-08 に 5 面のオーバレイ(s5.c)から移した。
   接近の戦艦の処理を足してオーバレイがあふれたため)。(x, y) = 左上(ドット)。G_MISC の 4 枚 */
#include "game.h"
#include "spr.h"
#include "sprite_data.h"

void fort_draw(s16 x, s16 y) {
    spr_put(x, y, PAT_FORT_TL, G_MISC, CS_DIM); spr_put((s16)(x + 16), y, PAT_FORT_TR, G_MISC, CS_DIM);
    spr_put(x, (s16)(y + 16), PAT_FORT_BL, G_MISC, CS_DIM); spr_put((s16)(x + 16), (s16)(y + 16), PAT_FORT_BR, G_MISC, CS_DIM);
}
