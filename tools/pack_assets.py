#!/usr/bin/env python3
"""pack_assets.py — 素材(.bin)を ROM のバンク 4 以降へ詰め、場所を C ヘッダに出す。

  usage: pack_assets.py <out.bin> <out.h> 名前=ファイル ...

out.bin は rompack.mjs の --asset 4 で置く(バンク 4 = ROM の 0x8000 から)。
out.h には素材ごとに ASSET_名前_BANK(8KB バンク番号)/ _OFF(バンク内の位置)/ _LEN を出す。
素材は 256 バイト境界にそろえて隙間なく並べる(読み出しの rom_to_vram はバンクをまたいでよい)。
"""
import sys

FIRST_BANK = 4
BANK = 8192
ALIGN = 256

def main(out_bin, out_h, items):
    blob = bytearray()
    lines = []
    for it in items:
        name, path = it.split('=', 1)
        data = open(path, 'rb').read()
        while len(blob) % ALIGN: blob.append(0xFF)
        pos = len(blob)
        blob += data
        lines.append(f'#define ASSET_{name}_BANK {FIRST_BANK + pos // BANK}\n'
                     f'#define ASSET_{name}_OFF  0x{pos % BANK:04X}\n'
                     f'#define ASSET_{name}_LEN  {len(data)}u\n')
    open(out_bin, 'wb').write(blob)
    with open(out_h, 'w') as f:
        f.write('/* assets.h — tools/pack_assets.py が生成(手で編集しない)。ROM の素材の場所。 */\n')
        f.write('#ifndef ASSETS_H\n#define ASSETS_H\n\n')
        f.write(''.join(lines))
        f.write(f'\n#define ASSETS_TOTAL {len(blob)}u\n\n#endif /* ASSETS_H */\n')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
