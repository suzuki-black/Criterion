#!/usr/bin/env python3
"""check_bank.py — 第 2 バンク(src/include/bank.h)から到達できる本体のコードが、本体のページ 2 にもオーバレイにも
触れないことを確かめる。

  check_bank.py <game.map> <game.noi> <第 2 バンクの .lst...>

第 2 バンクを出している間、本体のページ 2(0x8000-0xBFFF)は見えない。第 2 バンクが直接呼ぶ本体の関数はリンクで
ページ 2 の外に限っているが、その関数がさらにページ 2 の関数・ライブラリ・const の表を使うと壊れる(ROTOR が
ページ 2 の __mullong を呼んで暴走した)。そこで本体の .lst から「ラベル → 使う記号」の表を作り、第 2 バンクが
使う記号からたどれるものをすべて調べる。
  ・ラベルの番地は、同じモジュールの同じエリアの大域記号の番地と .lst の位置の差から求める(static も)。
  ・ラベルの塊が ret / jp / jr で終わらなければ、次のラベルへ流れ込むとみなす。
  ・間接の呼び出し(jp (hl) など)は行き先が分からないので失敗にする。
"""
import re
import sys

LABEL = re.compile(r'^([A-Za-z_.$][\w.$]*)::?')
IDENT = re.compile(r'[A-Za-z_][\w$.]*')
END = re.compile(r'^\s*(ret|reti|retn|jp\s+[^,]+|jr\s+[^,]+|halt)\s*(;.*)?$', re.I)
INDIRECT = re.compile(r'\b(jp|call)\s+\((hl|ix|iy)\)', re.I)


def parse_lst(path):
    """[(area, label, offset, refs, falls, indirect)]"""
    blocks = []
    area = '_CODE'
    cur = None
    for raw in open(path, errors='replace'):
        if len(raw) < 41:
            continue
        addr = raw[4:12].strip()
        src = raw[40:].rstrip('\n')          # 行番号(..39 桁目で右寄せ)の後ろがソース
        code = src.split(';', 1)[0]
        a = re.match(r'\s*\.area\s+(\w+)', code)
        if a:
            area = a.group(1)
            cur = None
            continue
        lm = LABEL.match(code)
        if lm and not re.match(r'^\d+\$', lm.group(1)):
            name = lm.group(1)
            off = int(addr, 16) if re.fullmatch(r'[0-9A-F]{8}', addr) else None
            if cur:
                cur['falls'] = not cur['ended']
            cur = {'area': area, 'name': name, 'off': off, 'refs': set(), 'ended': False,
                   'falls': False, 'indirect': False}
            blocks.append(cur)
            code = code[lm.end():]
        if cur is None:
            continue
        ins = code.strip()
        if not ins or ins.startswith('.'):
            if ins.startswith(('.dw', '.db')):
                cur['refs'].update(IDENT.findall(ins[3:]))
            continue
        if INDIRECT.search(ins):
            # pop hl → jp (hl) は SDCC の戻り(スタックの引数を払ってから戻る)
            if not (re.match(r'jp\s+\(hl\)', ins, re.I) and any(re.match(r'pop\s+hl', x, re.I) for x in cur.setdefault('recent', [])[-5:])):
                cur['indirect'] = True
            else:
                cur['ended'] = True
                cur.setdefault('recent', []).append(ins)
                continue
        cur.setdefault('recent', []).append(ins)
        parts = ins.split(None, 1)
        if len(parts) > 1:
            cur['refs'].update(t for t in IDENT.findall(parts[1])
                               if t.lower() not in ('a', 'b', 'c', 'd', 'e', 'h', 'l', 'af', 'bc', 'de', 'hl',
                                                    'ix', 'iy', 'sp', 'z', 'nz', 'nc', 'p', 'm', 'po', 'pe',
                                                    'i', 'r', 'ixh', 'ixl', 'iyh', 'iyl'))
        cur['ended'] = bool(END.match(ins)) and not re.match(r'^\s*(ret|jp|jr)\s+(z|nz|c|nc|p|m|po|pe)\b', ins, re.I)
    if cur:
        cur['falls'] = not cur['ended']
    return blocks


def main():
    mapf, noif = sys.argv[1], sys.argv[2]
    bk_lsts = sys.argv[3:]
    noi = {}
    for l in open(noif):
        p = l.split()
        if len(p) >= 3 and p[0] == 'DEF':
            noi[p[1]] = int(p[2], 16)
    # 本体にリンクしたモジュール
    rels = []
    sec = False
    for l in open(mapf):
        if l.startswith('Files Linked'):
            sec = True
            continue
        if sec:
            if l.startswith('Libraries Linked'):
                break
            m = re.match(r'^(\S+\.rel)\s', l)
            if m:
                rels.append(m.group(1))
    nodes = {}          # (mod, name) / name -> info
    glob = {}           # 大域名 -> key
    order = []
    for rel in rels:
        lst = rel[:-4] + '.lst'
        try:
            blocks = parse_lst(lst)
        except OSError:
            continue        # .lst の無いもの(ライブラリ・生成した .s)は noi の番地だけで見る
        base = {}
        for b in blocks:
            if b['name'] in noi and b['off'] is not None and b['area'] not in base:
                base[b['area']] = noi[b['name']] - b['off']
        prev = None
        for b in blocks:
            key = (rel, b['name'])
            b['addr'] = (base[b['area']] + b['off']) if (b['area'] in base and b['off'] is not None) else noi.get(b['name'])
            b['mod'] = rel
            nodes[key] = b
            if b['name'] in noi:
                glob[b['name']] = key
            if prev is not None and prev['falls'] and prev['area'] == b['area']:
                prev['next'] = key
            prev = b
    # 第 2 バンクが使う本体の記号
    bk_defs, bk_refs = set(), set()
    for lst in bk_lsts:
        for b in parse_lst(lst):
            bk_defs.add(b['name'])
            bk_refs |= b['refs']
    roots = sorted(r for r in bk_refs - bk_defs if r in noi)
    errors = []
    seen = set()
    stack = [(None, r, [r]) for r in roots]
    while stack:
        mod, name, path = stack.pop()
        key = (mod, name) if (mod, name) in nodes else glob.get(name, name)
        if key in seen:
            continue
        seen.add(key)
        info = nodes.get(key)
        addr = info['addr'] if info else noi.get(name)
        if addr is None:
            continue
        if 0x8000 <= addr < 0xC000:
            errors.append(f'本体のページ 2 の {name}(0x{addr:04X}): ' + ' → '.join(path))
            continue
        if 0x1000 <= addr < 0x3800:
            errors.append(f'オーバレイの {name}(0x{addr:04X}): ' + ' → '.join(path))
            continue
        if not info:
            continue
        if info['indirect']:
            errors.append(f'間接の呼び出し(行き先を調べられない) {name}: ' + ' → '.join(path))
        for r in info['refs']:
            stack.append((info['mod'], r, path + [r]))
        if 'next' in info:
            nm = info['next'][1]
            stack.append((info['mod'], nm, path + [f'({nm} へ流れ込む)']))
    if errors:
        for e in errors:
            print('ERROR: 第 2 バンクから ' + e)
        sys.exit(1)
    print(f'  第 2 バンク: 本体の記号 {len(roots)} 個から {len(seen)} 個をたどり、ページ 2・オーバレイに触れない')


if __name__ == '__main__':
    main()
