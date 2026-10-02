/* rom.h — ROM のデータバンク(ASCII8、8KB 単位)を VRAM / RAM へ読む。
   データの位置は tools/pack_assets.py が作る build/assets.h の ASSET_名前_BANK / _OFF / _LEN。 */
#ifndef ROM_H
#define ROM_H

#include "types.h"

/* (bank, off) から len バイトを VRAM の (vhi<<16 | vlo) へ。off は 8KB を超えてよい(次のバンクへ進む)。 */
void rom_to_vram(u8 bank, u16 off, u8 vhi, u16 vlo, u16 len);
/* 同じく RAM へ。★dst はページ 0 か 3(0x0000-0x3FFF / 0xC000-0xFFFF)。読む間はページ 1-2 がカートリッジになる。 */
void rom_to_ram(u8 bank, u16 off, u8 *dst, u16 len);

#endif /* ROM_H */
