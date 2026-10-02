/* font.h — 自前 8x8 フォント(SCREEN5)。 */
#ifndef FONT_H
#define FONT_H

#include "types.h"

void font_text(u8 page, u8 px, u8 py, u8 fg, u8 bg, const char *s);
void font_num(u8 page, u8 px, u8 py, u8 fg, u8 bg, u16 v, u8 digits);

#endif /* FONT_H */
