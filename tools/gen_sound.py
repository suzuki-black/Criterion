#!/usr/bin/env python3
"""gen_sound.py — 曲(MML)を snd.c のデータへ翻訳し、PCM と一緒に snd.bin(ROM のバンク SND_BANK から)へ詰める。

  usage: gen_sound.py <snd.bin> <snd_data.h>

曲の中身は tools/songs.py、PCM は tools/snd_pcm.py。形式は src/game/snd.c の頭を参照。

MML(1 チャンネル 1 文字列):
  c d e f g a b(+ # - で半音)、長さ 1 2 4 8 16 32(3 6 12 24 は 3 連)、%n は刻みで直接(24 刻み = 4 分)、. 付点、
  & タイ(同じ音ならつなげる、違う音ならレガート)、r 休み、o4 オクターブ、< > 1 オクターブ下・上、l8 既定の長さ、
  v12 音量(0..15)、@n 音色(FM: 0 = 曲のユーザー音色、1..15 = 内蔵)/ エンベロープ(PSG)、q3 音を切る刻み、
  D-2 デチューン、P名前 PCM、N1 / N0 ノイズ(PSG)、L ループの戻り先、T150 テンポ、[ ... ]3 くり返し、| は飾り。
  リズムのチャンネル: b(BD) s(SD) t(TOM) c(CYM) h(HH)の組み合わせ + 長さ(例: bh8 h8 sh8 h8)。
"""
import os, re, sys

sys.path.insert(0, os.path.dirname(__file__))

TPQ = 24                       # 4 分音符の刻み
SND_BANK = 64                  # ROM のバンク(Makefile と合わせる。素材が 6..47 に伸びたので 40 → 48、スプライトの色表が倍になって 64 へ)
NOTE = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}
CHN = ['FM0', 'FM1', 'FM2', 'FM3', 'FM4', 'FM5', 'RHY', 'PSGA', 'PSGB', 'PSGC']
RHY = {'b': 16, 's': 8, 't': 4, 'c': 2, 'h': 1}


class Err(Exception):
    pass


def ticks_of(num, dots):
    if num.startswith('%'):
        t = int(num[1:])
    else:
        n = int(num)
        if (TPQ * 4) % n:
            raise Err(f'長さ {n} は刻みで割り切れない')
        t = TPQ * 4 // n
    add = t
    for _ in range(dots):
        add //= 2
        t += add
    return t


def expand(s):
    """[ ... ]n を展開する(入れ子可)"""
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == '[':
            depth, j = 1, i + 1
            while depth:
                if j >= len(s):
                    raise Err('[ が閉じていない')
                if s[j] == '[':
                    depth += 1
                elif s[j] == ']':
                    depth -= 1
                j += 1
            m = re.match(r'\d+', s[j:])
            n = int(m.group()) if m else 2
            body = expand(s[i + 1:j - 1])
            out.append(' ' + ' '.join([body] * n) + ' ')
            i = j + (len(m.group()) if m else 0)
        else:
            out.append(c)
            i += 1
    return ''.join(out)


TOKEN = re.compile(r'\s+|\||([cdefgab])([+#-]*)(%\d+|\d+)?(\.*)(&?)|(r)(%\d+|\d+)?(\.*)|o(\d)|(<)|(>)|l(%\d+|\d+)(\.*)'
                   r'|v(\d+)|@(\d+)|q(\d+)|D(-?\d+)|P(\w+)|N([01])|(L)|T(\d+)')
RTOKEN = re.compile(r'\s+|\||([bstch]+)(%\d+|\d+)?(\.*)|(r)(%\d+|\d+)?(\.*)|(L)|v(\d+)|P(\w+)|T(\d+)')


def tempo_rate(bpm):
    return round(bpm * 256 / 150)          # 刻み × 256 / フレーム(60Hz、24 刻み = 4 分)


class Chan:
    def __init__(self, text, rhythm, pcm_ids, cue_bar):
        self.ev = []           # (tick, bytes, kind)
        self.loop_at = None    # ev の添字
        self.total = 0
        self.rhythm = rhythm
        self.pcm_ids = pcm_ids
        self.cue_bar = cue_bar
        self.parse(expand(text))

    def emit(self, b, kind='cmd'):
        self.ev.append((self.total, bytes(b), kind))

    def parse(self, s):
        oct, deflen, i = 4, 24, 0
        rx = RTOKEN if self.rhythm else TOKEN
        while i < len(s):
            m = rx.match(s, i)
            if not m or m.end() == i:
                raise Err(f'読めない: {s[i:i + 20]!r}')
            i = m.end()
            g = m.groups()
            if self.rhythm:
                if g[0]:
                    bits = 0
                    for ch in g[0]:
                        bits |= RHY[ch]
                    t = ticks_of(g[1], len(g[2])) if g[1] else deflen_dots(deflen, len(g[2]))
                    self.ev.append((self.total, (bits, t, False), 'note'))
                    self.total += t
                elif g[3]:
                    t = ticks_of(g[4], len(g[5])) if g[4] else deflen_dots(deflen, len(g[5]))
                    self.ev.append((self.total, t, 'rest'))
                    self.total += t
                elif g[6]:
                    self.loop_at = len(self.ev)
                elif g[7]:
                    self.emit([0x80 | int(g[7])])
                elif g[8]:
                    self.emit([0xA3, self.pcm_ids[g[8]]])
                elif g[9]:
                    r = tempo_rate(int(g[9]))
                    self.emit([0xA6, r & 255, r >> 8])
                continue
            if g[0]:
                n = oct * 12 + NOTE[g[0]]
                for a in g[1]:
                    n += 1 if a in '+#' else -1
                t = ticks_of(g[2], len(g[3])) if g[2] else deflen_dots(deflen, len(g[3]))
                if not 0 <= n < 96:
                    raise Err(f'音域の外: {n}')
                self.ev.append((self.total, (n, t, bool(g[4])), 'note'))
                self.total += t
            elif g[5]:
                t = ticks_of(g[6], len(g[7])) if g[6] else deflen_dots(deflen, len(g[7]))
                self.ev.append((self.total, t, 'rest'))
                self.total += t
            elif g[8]:
                oct = int(g[8])
            elif g[9]:
                oct -= 1
            elif g[10]:
                oct += 1
            elif g[11]:
                deflen = ticks_of(g[11], len(g[12]))
            elif g[13]:
                self.emit([0x80 | min(15, int(g[13]))])
            elif g[14]:
                self.emit([0x90 | int(g[14])])
            elif g[15]:
                self.emit([0xA0, int(g[15])])
            elif g[16]:
                self.emit([0xA1, int(g[16]) & 255])
            elif g[17]:
                self.emit([0xA3, self.pcm_ids[g[17]]])
            elif g[18]:
                self.emit([0xA5, int(g[18])])
            elif g[19]:
                self.loop_at = len(self.ev)
            elif g[20]:
                r = tempo_rate(int(g[20]))
                self.emit([0xA6, r & 255, r >> 8])

    def reverse(self):
        """音符と休符の並びを後ろから前へ(★曲の逆再生。指示書 6.3 の逆再生シンバルの代わり)。ループの区間だけを逆にし、
        前奏は捨てる。音量・音色・切り・デチューン・ノイズは、各音符の時点の値を保って付け直す。同じ音へのタイはつないでから逆にし、
        違う音へのタイ(レガート)は外す。PCM の合図は外す(逆の曲で鳴っても意味が合わない)"""
        start = self.loop_at if self.loop_at is not None else 0
        head, body = self.ev[:start], self.ev[start:]
        state, items, k = {}, [], 0
        for _, val, kind in head:                            # 前奏の音符は捨て、決めた音色・音量だけ引き継ぐ(全声部で捨てるので長さはそろう)
            if kind == 'cmd' and val[0] not in (0xA3, 0xA6):
                state[val[0] & 0xF0 if val[0] < 0xA0 else val[0]] = val
        head = []
        while k < len(body):
            tick, val, kind = body[k]
            if kind == 'cmd':
                op = val[0]
                if op == 0xA3 or op == 0xA6:
                    pass                                     # PCM・テンポは逆の側では使わない(テンポは曲の頭のまま)
                else:
                    state[op & 0xF0 if op < 0xA0 else op] = val
            elif kind == 'rest':
                items.append((tuple(sorted(state.items())), val, 'rest'))
            else:
                n, t, tie = val
                while tie and k + 1 < len(body) and body[k + 1][2] == 'note' and body[k + 1][1][0] == n:
                    k += 1
                    t += body[k][1][1]
                    tie = body[k][1][2]
                items.append((tuple(sorted(state.items())), (n, t, False), 'note'))
            k += 1
        ev = []
        tick = 0
        self.loop_at = len(ev)
        prev = None
        for st, val, kind in reversed(items):
            if st != prev:
                for op, b in st:
                    if prev is None or dict(prev).get(op) != b:
                        ev.append((tick, b, 'cmd'))
                prev = st
            ev.append((tick, val, kind))
            tick += val if kind == 'rest' else val[1]
        self.ev = ev
        self.total = tick

    def code(self, base, loop):
        """bytes と、ループの戻り先(base からの相対)"""
        out = bytearray()
        loop_off = None
        next_bar = 0
        k = 0
        evs = self.ev
        while k < len(evs):
            tick, val, kind = evs[k]
            if k == (self.loop_at if self.loop_at is not None else 0):
                loop_off = len(out)
            if self.cue_bar and kind in ('note', 'rest'):
                while next_bar <= tick:
                    out += b'\xA4'
                    next_bar += self.cue_bar
            if kind == 'cmd':
                out += val
            elif kind == 'rest':
                t = val
                first = True
                while t > 0:
                    d = min(255, t)
                    out += bytes([0x60 if first else 0x61, d])
                    first = False
                    t -= d
            else:
                n, t, tie = val
                # 同じ音へのタイはつなげる
                while tie and k + 1 < len(evs) and evs[k + 1][2] == 'note' and evs[k + 1][1][0] == n:
                    k += 1
                    t += evs[k][1][1]
                    tie = evs[k][1][2]
                d = min(255, t)
                out += bytes([n, d])
                t -= d
                while t > 0:
                    d = min(255, t)
                    out += bytes([0x61, d])
                    t -= d
                if tie:
                    out += b'\xA2'          # 次の(違う)音をつなぐ
            k += 1
        if loop_off is None:
            loop_off = len(out)
        if loop:
            a = base + loop_off
            out += bytes([0xFE, a & 255, a >> 8])
        else:
            out += b'\xFF'
        return bytes(out)

    def loop_len(self):
        start = self.ev[self.loop_at][0] if self.loop_at is not None and self.loop_at < len(self.ev) else 0
        return self.total - start


def deflen_dots(t, dots):
    add = t
    for _ in range(dots):
        add //= 2
        t += add
    return t


def compile_song(song, base, pcm_ids, song_ids):
    """song: songs.Song。base = ROM の窓での番地(0x8000..)"""
    chans = {}
    cue_done = False
    for name in CHN:
        if name in song.ch:
            try:
                chans[name] = Chan(song.ch[name], name == 'RHY', pcm_ids, None if cue_done else song.bar)
                if getattr(song, 'rev', False):
                    chans[name].reverse()
            except Err as e:
                raise Err(f'{song.id} {name}: {e}')
            cue_done = True
    lens = {n: c.loop_len() for n, c in chans.items()}
    if song.loop and len(set(lens.values())) > 1 and not song.free:
        raise Err(f'{song.id}: ループの長さがそろわない {lens}')
    head = bytearray()
    r = tempo_rate(song.bpm)
    head += bytes([r & 255, r >> 8, song_ids[song.next] if song.next else 0xFF])
    head += bytes(song.user)
    body = bytearray()
    addrs = []
    for name in CHN:
        if name not in chans:
            addrs.append(0)
            continue
        a = base + 31 + len(body)
        addrs.append(a)
        body += chans[name].code(a, song.loop)
    for a in addrs:
        head += bytes([a & 255, a >> 8])
    assert len(head) == 31
    return bytes(head + body), max(c.total for c in chans.values())


def main(binpath, hpath):
    import songs, snd_pcm
    pcms = snd_pcm.build()                       # [(id, prio, data)]
    pcm_ids = {p[0]: i for i, p in enumerate(pcms)}
    song_ids = {s.id: i for i, s in enumerate(songs.SONGS)}
    out = bytearray()
    srows = []
    for s in songs.SONGS:
        off = len(out)
        if off % 8192 > 8192 - 64:                # 頭が窓の端にかからないように
            out += bytes(8192 - off % 8192)
            off = len(out)
        base = 0x8000 + off % 8192
        data, total = compile_song(s, base, pcm_ids, song_ids)
        if len(data) > 8192:
            raise Err(f'{s.id}: {len(data)} バイト(8KB まで)')
        out += data
        srows.append((s.id, SND_BANK + off // 8192, base, len(data), total))
    prow = []
    for pid, prio, data in pcms:
        off = len(out)
        out += bytes(data)
        prow.append((pid, SND_BANK + off // 8192, off % 8192, len(data), prio))
    open(binpath, 'wb').write(out)
    with open(hpath, 'w') as f:
        f.write('/* snd_data.h — tools/gen_sound.py が生成(手で編集しない)。曲と PCM の位置。\n')
        f.write('   ★表は SND_TABLES を定義したファイル(snd.c)にだけ出す。 */\n#ifndef SND_DATA_H\n#define SND_DATA_H\n\n')
        f.write(f'#define SND_NSONG {len(srows)}\n#define SND_NPCM {len(prow)}\n')
        for i, r in enumerate(srows):
            f.write(f'#define SONG_{r[0]} {i}   /* {r[3]} バイト、{r[4] / TPQ / 4:.1f} 小節 */\n')
        for i, r in enumerate(prow):
            f.write(f'#define PCM_{r[0]} {i}   /* {r[3]} バイト、{r[3] / snd_pcm.RATE:.2f} 秒 */\n')
        f.write('\n#ifdef SND_TABLES\n')
        f.write('const u8 snd_song_bank[SND_NSONG] = { ' + ', '.join(str(r[1]) for r in srows) + ' };\n')
        f.write('const u16 snd_song_addr[SND_NSONG] = { ' + ', '.join(hex(r[2]) for r in srows) + ' };\n')
        f.write('const u8 snd_pcm_bank[SND_NPCM] = { ' + ', '.join(str(r[1]) for r in prow) + ' };\n')
        f.write('const u16 snd_pcm_off[SND_NPCM] = { ' + ', '.join(str(r[2]) for r in prow) + ' };\n')
        f.write('const u16 snd_pcm_len[SND_NPCM] = { ' + ', '.join(str(r[3]) for r in prow) + ' };\n')
        f.write('const u8 snd_pcm_prio[SND_NPCM] = { ' + ', '.join(str(r[4]) for r in prow) + ' };\n')
        f.write('#endif\n\n#endif /* SND_DATA_H */\n')
    print(f'snd.bin: {len(out)} バイト(バンク {SND_BANK}..{SND_BANK + (len(out) - 1) // 8192})、曲 {len(srows)}、PCM {len(prow)}')


if __name__ == '__main__':
    try:
        main(sys.argv[1], sys.argv[2])
    except Err as e:
        print('ERROR:', e)
        sys.exit(1)
