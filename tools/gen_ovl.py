#!/usr/bin/env python3
"""gen_ovl.py — オーバレイ(src/include/ovl.h)を作るための小物。

  gen_ovl.py hdr <out.s> <名前...>                 オーバレイの先頭の JP 表(リンクの最初に置く)
  gen_ovl.py entries <out.s> <base> <名前...>      本体側: 名前 == base + 3 × 番号(名前は 'a,b,c' で同じ番号に複数)
  gen_ovl.py resident <game.noi> <out.s> <除く名前...>
                                                   本体の大域シンボルを絶対番地で定義する(オーバレイから呼ぶため)
  gen_ovl.py resident_bk <game.noi> <out.s> <除く名前...>
                                                   第 2 バンク用: 同じく、ただしページ 2(0x8000-0xBFFF)の記号と _bk_ で始まる
                                                   記号を除く(第 2 バンクを出している間、本体のページ 2 は見えない)
  gen_ovl.py bkstubs <out.s> <名前...> [-- <名前...> [-- <名前...>]]
                                                   本体側: 第 2(と第 3)バンクの関数の呼び口(call _bk_call / .db 番号 × 3。
                                                   第 3 バンクは番号に 0x80 を足す)
  gen_ovl.py bin <in.ihx> <in.map> <out.bin> <base> <end> <data_base> <data_end>
                                                   .ihx の base..end をバイナリに。範囲外・初期値つき変数・データの
                                                   はみ出しを検査する
"""
import sys

def hdr(out, names):
    with open(out, 'w') as f:
        f.write('; gen_ovl.py が生成(オーバレイの入口の JP 表)\n\t.module ovlhdr\n\t.area _CODE\n')
        for n in names: f.write(f'\t.globl _{n}\n')
        for n in names: f.write(f'\tjp _{n}\n')

def entries(out, base, groups):
    base = int(base, 0)
    with open(out, 'w') as f:
        f.write('; gen_ovl.py が生成(オーバレイの入口の番地。本体からはこの番地を呼ぶ)\n\t.module ovl_entries\n')
        for i, g in enumerate(groups):
            for n in g.split(','):
                f.write(f'_{n} == 0x{base + 3 * i:04X}\n')

def resident(noi, out, exclude, bank=False):
    ex = set('_' + e for e in exclude)
    syms = []
    for l in open(noi):
        p = l.split()
        if len(p) >= 3 and p[0] == 'DEF' and p[1].startswith('_') and p[1] not in ex:
            v = int(p[2], 16)
            if bank and (0x8000 <= v < 0xC000 or p[1].startswith('_bk_')): continue
            syms.append((p[1], v))
    with open(out, 'w') as f:
        f.write('; gen_ovl.py が生成(本体の大域シンボル。オーバレイのリンクで使う)\n\t.module resident\n')
        for n, v in syms: f.write(f'{n} == 0x{v:04X}\n')

def bkstubs(out, names):
    """名前... [-- 第 3 バンクの名前... [-- 第 4 バンクの名前...]]。番号 0x00..0x7F = 第 2 バンク、0x80..0xBF = 第 3 バンク、
    0xC0..0xFF = 第 4 バンク(rom.c の bk_call が上位 2 ビットでセグメントを選ぶ)"""
    groups = [[]]
    for n in names:
        if n == '--': groups.append([])
        else: groups[-1].append(n)
    groups += [[]] * (3 - len(groups))
    with open(out, 'w') as f:
        f.write('; gen_ovl.py が生成(第 2〜第 4 バンクの関数の呼び口。rom.c の bk_call が番号の JP 表へ飛ぶ)\n'
                '\t.module bkstubs\n\t.area _CODE\n\t.globl _bk_call\n')
        for names_, base, what, lim in ((groups[0], 0, '第 2', 42), (groups[1], 0x80, '第 3', 21), (groups[2], 0xC0, '第 4', 21)):
            if len(names_) > lim: sys.exit(f'ERROR: {what}バンクの入口が多すぎる({lim} まで)')
            for i, n in enumerate(names_):
                f.write(f'_{n}::\n\tcall _bk_call\n\t.db {base + 3 * i}\n')

def binary(ihx, mapf, out, base, end, dbase, dend):
    base, end, dbase, dend = (int(x, 0) for x in (base, end, dbase, dend))
    mem = {}
    for l in open(ihx):
        if not l.startswith(':') or l[7:9] != '00': continue
        n = int(l[1:3], 16); a = int(l[3:7], 16)
        for i in range(n):
            ad = a + i
            if not base <= ad < end:
                sys.exit(f'ERROR: オーバレイのバイトが範囲外: 0x{ad:04X}(0x{base:04X}..0x{end:04X})')
            mem[ad] = int(l[9 + 2 * i:11 + 2 * i], 16)
    size = (max(mem) + 1 - base) if mem else 0
    areas = {}
    for l in open(mapf):
        p = l.split()
        if len(p) >= 3 and p[0].startswith('_') and len(p[1]) == 8 and len(p[2]) == 8:
            try: areas[p[0]] = (int(p[1], 16), int(p[2], 16))
            except ValueError: pass
    if areas.get('_INITIALIZER', (0, 0))[1]:
        sys.exit('ERROR: オーバレイに初期値つきの変数がある(crt0 が初期化しない。ovl.h)')
    if '_DATA' in areas:
        a, l = areas['_DATA']
        if l and (a < dbase or a + l > dend):
            sys.exit(f'ERROR: オーバレイの変数がはみ出した: 0x{a:04X}+{l}(0x{dbase:04X}..0x{dend:04X})')
    open(out, 'wb').write(bytes(mem.get(base + i, 0xFF) for i in range(size)))
    d = areas.get('_DATA', (0, 0))[1]
    print(f'  オーバレイ {out}: コード {size}B / {end - base}B、変数 {d}B / {dend - dbase}B')

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'hdr': hdr(sys.argv[2], sys.argv[3:])
    elif cmd == 'entries': entries(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif cmd == 'resident': resident(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif cmd == 'resident_bk': resident(sys.argv[2], sys.argv[3], sys.argv[4:], True)
    elif cmd == 'bkstubs': bkstubs(sys.argv[2], sys.argv[3:])
    elif cmd == 'bin': binary(*sys.argv[2:9])
    else: sys.exit('usage: gen_ovl.py hdr|entries|resident|resident_bk|bkstubs|bin ...')
