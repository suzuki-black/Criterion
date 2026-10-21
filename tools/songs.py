#!/usr/bin/env python3
"""songs.py — Criterion の曲(指示書 6.3)。すべて本作のための書き下ろし。gen_sound.py が翻訳する。

  ライトモチーフ(指示書 6.2):
    Infinitas の動機 … 根音 → 5 度 → オクターブ → さらに 2 度上、と跳躍して上へ抜け、終止しない(INF)。
    Criterion の動機 … 同じ音を均等に刻み続け、最後に半音下へ“判定”を下す(CRI)。
  チャンネル: FM0..FM5(FM)、RHY(リズム)、PSGA..PSGC。1 曲に 1 つのユーザー音色(user)。
  伴奏(ベース・アルペジオ・パッド・刻み)は和音の進行から作り、旋律は手で書く。
"""

NAMES = ['c', 'c+', 'd', 'd+', 'e', 'f', 'f+', 'g', 'g+', 'a', 'a+', 'b']
PC = {'C': 0, 'C#': 1, 'Db': 1, 'D': 2, 'D#': 3, 'Eb': 3, 'E': 4, 'F': 5, 'F#': 6, 'Gb': 6, 'G': 7,
      'G#': 8, 'Ab': 8, 'A': 9, 'A#': 10, 'Bb': 10, 'B': 11}
QUAL = {'': (0, 4, 7), 'm': (0, 3, 7), '7': (0, 4, 7, 10), 'm7': (0, 3, 7, 10), 'maj7': (0, 4, 7, 11),
        'dim': (0, 3, 6), 'sus4': (0, 5, 7), 'aug': (0, 4, 8), '5': (0, 7, 12), 'add9': (0, 4, 7, 14),
        'm9': (0, 3, 7, 14)}

# ---- OPLL のユーザー音色(8 バイト: 変調器/搬送波の AM VIB EG KSR MULT、TL、KSL/波形/FB、AR DR、SL RR) ----
BRASS = [0x21, 0x21, 0x16, 0x05, 0x71, 0xF2, 0x14, 0x07]
STRINGS = [0x61, 0x61, 0x1C, 0x04, 0x52, 0x62, 0x14, 0x16]
CHOIR = [0x62, 0x61, 0x0E, 0x02, 0x40, 0x50, 0x16, 0x16]
ORGAN = [0x22, 0x21, 0x1E, 0x00, 0xF0, 0xF0, 0x0F, 0x0F]
SYNBASS = [0x01, 0x01, 0x18, 0x05, 0xF3, 0xF4, 0x24, 0x26]
GLASS = [0x13, 0x11, 0x0C, 0x00, 0xF3, 0xF2, 0x23, 0x24]
LEAD = [0x31, 0x21, 0x12, 0x06, 0xF1, 0xF3, 0x12, 0x05]
# ★メロディーは必ず自作音色にする(M7 の後。聴き比べで決めた。自作音色は 1 曲に 1 つなので、パッドに使っていた曲はパッドを内蔵 @1 へ)
GTR = [0x21, 0x61, 0x0E, 0x0F, 0xF3, 0xF1, 0x24, 0x15]      # 哀愁のギター(M04)。変調器を半波・帰還 7 で歪ませたリード
VIBE = [0x14, 0x81, 0x1A, 0x00, 0xF5, 0xF2, 0x64, 0xF4]     # ビブラフォン(M05)。変調 4 倍・トレモロ・長い減衰
HARP = [0x13, 0x11, 0x14, 0x05, 0xF4, 0xF3, 0x55, 0xF5]     # ハープシコード(M09)。変調 3 倍の爪弾き
TPT = [0x21, 0x61, 0x12, 0x06, 0x82, 0xF2, 0x13, 0x05]      # 金管のリード(M11)。変調器の立ち上がりを遅く
FLUTE = [0x22, 0x61, 0x2A, 0x00, 0x61, 0x71, 0x11, 0x06]    # オカリナ寄りの笛(M16)
# 内蔵音色: 1 Violin 2 Guitar 3 Piano 4 Flute 5 Clarinet 6 Oboe 7 Trumpet 8 Organ 9 Horn 10 Synth
#           11 Harpsichord 12 Vibraphone 13 Synth bass 14 Wood bass 15 E.guitar


class Song:
    def __init__(self, id, bpm, ch, bar=96, next=None, user=BRASS, loop=True, free=False):
        self.id, self.bpm, self.ch, self.bar, self.next = id, bpm, ch, bar, next
        self.user, self.loop, self.free = user, loop, free


def chord(sym):
    root = sym[:2] if len(sym) > 1 and sym[1] in '#b' else sym[:1]
    return PC[root], QUAL[sym[len(root):]]


def nn(n, l):
    return f'o{n // 12}{NAMES[n % 12]}{l}'


def tones(sym, lo):
    """和音の構成音を lo 以上の最初の位置から"""
    r, q = chord(sym)
    out = []
    for iv in q:
        n = (r + iv) % 12
        while n < lo:
            n += 12
        out.append(n + (iv // 12) * 12)
    return sorted(out)


def steps(bar, step):
    return bar // step


def bass(prog, oct=2, pat=(0, 0, 12, 0, 0, 12, 0, 12), step=12, bar=96, vol=None):
    out = ''
    for sym in prog:
        r, _ = chord(sym)
        b = oct * 12 + r
        k = steps(bar, step)
        out += ' '.join(nn(b + pat[i % len(pat)], f'%{step}') for i in range(k)) + ' '
    return out


def bass5(prog, oct=2, step=12, bar=96):
    """根音と 5 度を交互に"""
    out = ''
    for sym in prog:
        r, q = chord(sym)
        b = oct * 12 + r
        k = steps(bar, step)
        out += ' '.join(nn(b + (7 if i % 2 else 0), f'%{step}') for i in range(k)) + ' '
    return out


def arp(prog, lo, step=6, order=(0, 1, 2, 3), bar=96):
    out = ''
    for sym in prog:
        t = tones(sym, lo)
        t = t + [t[0] + 12, t[1] + 12, t[2] + 12]
        k = steps(bar, step)
        out += ' '.join(nn(t[order[i % len(order)]], f'%{step}') for i in range(k)) + ' '
    return out


def rot(mml_notes, k=1):
    """アルペジオのくり返しを k 音ずらす(PSG の疑似ディレイ)"""
    toks = mml_notes.split()
    return ' '.join(toks[-k:] + toks[:-k])


def pad(prog, which, lo, bar=96):
    out = ''
    prev = None
    for sym in prog:
        n = tones(sym, lo)[which]
        out += nn(n, f'%{bar}') + ' '
    return out


def stab(prog, lo, which=(0, 1, 2), bar=96, step=12):
    """裏拍の刻み(1 声ぶん: which の音)"""
    out = ''
    for sym in prog:
        t = tones(sym, lo)
        k = steps(bar, step)
        out += ' '.join(('r' + f'%{step}') if i % 2 == 0 else nn(t[which[0]], f'%{step}') for i in range(k)) + ' '
    return out


def rep(s, n):
    return ' '.join([s] * n)


# ================================================================ 曲
SONGS = []

# ---- M01 Absolute Scale(タイトル): 92 で積み上げ → 132 で金管の Infinitas の動機 ----
m01_prog = ['Dm', 'Bb', 'C', 'Dm', 'Gm', 'Bb', 'A', 'A']
SONGS.append(Song('M01', 92, {
    'FM0': 'v0 r1 r1 r1 r1 L T132 v13 @0 q2 o4d4 a4 o5d4 e4 | f2 e4 d4 | e4. c8 o4g4 o5c4 | d1 | '
           'o4d4 a4 o5d4 e4 | a2 g4 f4 | e2. c+4 | e1',
    'FM1': '@1 v11 o4f1& f1& f1& f1 L ' + pad(m01_prog, 1, 60),
    'FM2': '@1 v10 o4a1& a1& a1& a1 L ' + pad(m01_prog, 2, 64),
    'FM3': '@14 v13 o2d1& d1& d1& d1 L q4 ' + bass(m01_prog, 2, (0, 0, 7, 12), 24),
    'FM4': '@1 v9 r1 o2a1& a1& a1 L ' + pad(m01_prog, 0, 50),
    'FM5': '@1 v9 r1 r1 o3d1& d1 L r1 r1 r1 r1 r1 r1 r1 r1',
    'RHY': 'r1 r1 r1 r1 L PP01 b4 h8 h8 bs8 h8 h8 h8 | ' + rep('b4 h8 h8 bs8 h8 h8 h8', 6) + ' | b8 b8 s8 s8 bs8 s8 sc4',
    'PSGA': '@2 v9 q2 r1 r1 r1 r1 L ' + arp(m01_prog, 62, 12, (0, 1, 2, 1)),
    # ★PSG で厚みを足す(遊んでの指摘「ドラムがポコポコ、メロディーに厚みがない」。聴き比べで C 案に決めた):
    #   B = 旋律を同じ高さで重ねる(D2 = 周期 +2 で少し低く、FM の金管とうならせる)、C = ノイズのスネア(v13、約 9 フレーム)と
    #   閉じたハイハット(v9、約 4 フレーム)。シンバルは FM のまま。★C は効果音と取り合うので、ゲーム中の曲では置き場を考える
    'PSGB': 'r1 r1 r1 r1 L @0 v9 q2 D2 o4d4 a4 o5d4 e4 | f2 e4 d4 | e4. c8 o4g4 o5c4 | d1 | '
            'o4d4 a4 o5d4 e4 | a2 g4 f4 | e2. c+4 | e1',
    'PSGC': 'r1 r1 r1 r1 L N1 @1 ' + rep('r4 q9 v9 o5d+8 q9 v9 o5d+8 q4 v13 o4e8 q9 v9 o5d+8 q9 v9 o5d+8 q9 v9 o5d+8', 7) +
            ' r8 r8 q4 v13 o4e8 o4e8 o4e8 o4e8 q8 o4e4',
}, user=BRASS))

# ---- M02 Sortie(出撃 6 秒): 音声 → Infinitas の動機の短いファンファーレ → オケヒ 3 連 → 空中戦へ ----
# ★切れ目なく刻み続け、最後の小節は B7(空中戦のホ短調の属和音)で D# を伸ばして、空中戦の頭の E へ半音で解決する。
#   最初の版は「2 分音符 + 2 分休符」「全音符」で後半が止まりかけ、もたついて聞こえた(M7)
m02 = ['Bb', 'Eb', 'Gm', 'B7']
SONGS.append(Song('M02', 140, {
    'FM0': '@0 v14 q2 o4a+8 o5f8 a+8 o6c8 d4. c8 | o5a+4 o6d4 f4 g4 | a+8 g8 f8 d8 c4 d8 f8 | d+2.& d+4',
    'FM1': '@1 v11 o4f2 f2 | g2 g2 | g4 g4 e4 e4 | f+1',
    'FM2': '@1 v11 o4d2 d2 | d+2 d+2 | d4 d4 c4 c4 | a1',
    'FM3': '@14 v13 q2 ' + bass(m02, 2, (0, 0, 12, 0), 12),
    'RHY': 'PV01 b8 h8 h8 h8 sh8 h8 bh8 h8 | bh8 h8 sh8 h8 bh8 bh8 s16 s16 s16 s16 | '
           'PP01 bc8 h8 PP01 bc8 h8 PP01 bc8 h8 s16 s16 s8 | s16 s16 s16 s16 s16 s16 s16 s16 s16 s16 s16 s16 s16 s16 s16 s16',
    'PSGA': '@2 v10 q3 ' + arp(m02, 65, 6, (0, 1, 2, 3)),
}, next='M03', loop=False))

# ---- M03 Intercept Vector(空中戦、全面共通。面ごとに移調) ----
m03_a = ['Em', 'C', 'D', 'Bm', 'Em', 'C', 'Am', 'B']
m03_b = ['C', 'D', 'Em', 'Em', 'C', 'D', 'B', 'B7']
m03 = m03_a + m03_b
SONGS.append(Song('M03', 150, {
    'FM0': '@0 v13 q2 '
           'o5e4. f+8 g4 a4 | g8 f+8 e8 d8 e2 | f+4. g8 a4 b4 | a8 g8 f+8 d8 f+2 | '
           'e4. f+8 g4 a4 | b4 o6c4 o5b4 a4 | g8 a8 b8 o6c8 d4 e4 | d+2 r2 | '
           'o6c4. d8 e4 g4 | f+4. e8 d4 a4 | o5b4 o6e8 f+8 g4 f+8 e8 | e1 | '
           'o6c4 e8 d8 c4 o5b4 | a4 o6d8 c8 o5b4 a4 | b2 o6d+4 f+4 | b2 a2',
    'FM1': '@2 v10 q4 ' + stab(m03, 64, (0,)),
    'FM2': '@1 v9 ' + pad(m03, 1, 60),
    'FM3': '@1 v9 ' + pad(m03, 2, 62),
    'FM4': '@13 v13 q3 ' + bass(m03, 2, (0, 0, 12, 0, 0, 12, 0, 0, 12, 0, 0, 12, 0, 7, 12, 0), 6),
    'RHY': 'L ' + rep('bh8 h8 sh8 h8 bh8 bh8 sh8 h8', 3) + ' PP05 bh8 h8 sh8 h8 bh8 s16 s16 s8 sc8 | '
           + rep('bh8 h8 sh8 h8 bh8 bh8 sh8 h8', 3) + ' PP05 bc8 h8 sh8 h8 bh8 s16 s16 s8 s8 | '
           + rep('bh8 h8 sh8 h8 bh8 bh8 sh8 h8', 3) + ' PP05 bh8 h8 sh8 h8 b16 b16 s16 s16 s8 sc8 | '
           + rep('bh8 h8 sh8 h8 bh8 bh8 sh8 h8', 2) + ' b8 s8 b8 s8 b8 s8 s16 s16 s16 s16 | PP05 bc8 bc8 s8 s8 s16 s16 s16 s16 sc4',
    'PSGA': '@1 v10 ' + arp(m03, 64, 6, (0, 1, 2, 3, 2, 1, 0, 1)),
    'PSGB': '@1 v6 ' + rot(arp(m03, 64, 6, (0, 1, 2, 3, 2, 1, 0, 1)), 3),
}, user=LEAD))

# ---- M04 Dusk Over Ruins(1 面): 哀愁のギター、行進するタム ----
m04 = ['Am', 'F', 'G', 'Em', 'Am', 'F', 'E', 'E', 'F', 'G', 'Am', 'Am', 'Dm', 'E', 'Am', 'E7']
SONGS.append(Song('M04', 132, {
    'FM0': 'L @0 v13 q2 '
           'o5e4. d8 c4 o4b4 | a2. o5c4 | d4. c8 o4b4 g4 | e2. r4 | '
           'o5e4. d8 c4 e4 | a2 g4 f4 | e4 g+4 b4 o6d4 | c4 o5b4 g+2 | '
           'a4. g8 f4 e4 | d4 e4 f4 g4 | e4. d8 c4 o4b4 | a1 | '
           'o5f4. e8 d4 a4 | g+4 b4 o6d4 e4 | c4. o5b8 a4 e4 | g+2 b2',
    'FM1': 'L @1 v9 ' + pad(m04, 1, 60),
    'FM2': 'L @1 v9 ' + pad(m04, 2, 62),
    'FM3': 'L @14 v13 q4 ' + bass5(m04, 2, 24),
    'RHY': 'PP02 L ' + rep('bt4 t8 t8 st4 t8 t8', 7) + ' bt4 t8 t8 s8 s8 sc4 | '
           + rep('bt4 t8 t8 st4 t8 t8', 7) + ' bt8 t8 t8 t8 s16 s16 s16 s16 sc4',
    'PSGA': 'L @3 v8 ' + arp(m04, 69, 12, (0, 1, 2, 3)),
}, user=GTR))

# ---- M05 Weightless(2 面): F# リディア、ハーフタイム、残響の長いアルペジオ(PSG の疑似ディレイ) ----
m05 = ['F#maj7', 'G#/F#', 'F#maj7', 'G#/F#', 'D#m7', 'G#', 'F#add9', 'F#add9']
m05c = ['F#maj7', 'G#', 'F#maj7', 'G#', 'D#m7', 'G#', 'F#add9', 'F#add9']
m05_arp = arp(m05c, 66, 6, (0, 2, 1, 3, 4, 2, 5, 3))
SONGS.append(Song('M05', 112, {
    'FM0': 'L @0 v12 q0 o5c+2. o4a+4 | o5c2 d+2 | f+2. f4 | g+1 | f+2. d+4 | c2 o4a+4 o5c4 | c+1& | c+1',
    'FM1': 'L @1 v9 ' + pad(m05c, 0, 54),
    'FM2': 'L @1 v8 ' + pad(m05c, 2, 58),
    'FM3': 'L @14 v12 o2f+1 f+1 f+1 f+1 d+1 g+1 f+1 f+1',
    'RHY': 'L ' + rep('b4 h8 h8 s4 h8 h8', 6) + ' b4 h8 h8 s8 h8 sh8 h8 | b4 h4 s4 c4',
    'PSGA': 'L @7 v10 ' + m05_arp,
    'PSGB': 'L @7 v6 ' + rot(m05_arp, 2),
}, user=VIBE))

# ---- M06 Redline(3 面): 加速曲。速さはスクロールに合わせる(g_snd_tempo) ----
m06 = ['C#m', 'A', 'B', 'C#m', 'C#m', 'A', 'B', 'G#7']
SONGS.append(Song('M06', 168, {
    'FM0': 'L @0 v13 q2 o5c+8 c+8 e8 c+8 g+4 f+8 e8 | a4 g+8 f+8 e4 c+4 | f+8 f+8 a8 f+8 b4 a8 g+8 | g+2. r4 | '
           'o5c+8 e8 g+8 o6c+8 o5b4 g+4 | a4 o6c+4 e4 d+8 c+8 | o5b4. a8 g+4 f+4 | g+2 c2',
    'FM1': 'L @15 v10 q3 ' + stab(m06, 61, (0,)),
    'FM2': 'L @1 v9 ' + pad(m06, 1, 60),
    'FM3': 'L @13 v13 q2 ' + bass(m06, 2, (0, 0, 12, 0), 6),
    'RHY': 'L ' + rep('bh8 h8 sh8 bh8 h8 bh8 sh8 h8', 7) + ' bc8 s16 s16 s8 s16 s16 s8 s8 s16 s16 s16 s16',
    'PSGA': 'L @1 v9 ' + arp(m06, 68, 6, (0, 1, 2, 1)),
}, user=LEAD))

# ---- M07 Strata(4 面): D ドリア、3 対 4。層ごとに消音を外していく(g_snd_mute) ----
m07 = ['Dm7', 'G', 'Dm7', 'G', 'Am7', 'C', 'Dm7', 'G', 'Fmaj7', 'G', 'Am7', 'Dm7']
three = ' '.join(nn(n, '8') for n in [62, 69, 72]) + ' '      # 3 つの 8 分のくり返し(4/4 の上で 3 小節で一巡)
SONGS.append(Song('M07', 128, {
    'FM0': 'L @0 v12 q3 ' + rep(three, 32),
    'FM1': 'L @3 v11 q2 o5d4 f4 e4 c4 | o4b2 o5d2 | f4 a4 g4 e4 | d2 o4b2 | o5c4 e4 d4 c4 | e2 g2 | f4 a4 o6c4 o5a4 | b2 g2 | '
           'a4 g4 f4 e4 | d2 g2 | e4 c4 o4a4 o5c4 | d1',
    'FM2': 'L @1 v9 ' + pad(m07, 1, 60),
    'FM3': 'L @14 v12 q3 ' + bass(m07, 2, (0, 7, 12, 7), 24),
    'RHY': 'L ' + rep('b8 h8 h8 b8 h8 h8 s8 h8', 11) + ' b8 h8 s8 b8 s8 s8 sc4',
    'PSGA': 'L @2 v9 ' + arp(m07, 74, 8, (0, 1, 2)),
    'PSGB': 'L @3 v8 ' + rot(arp(m07, 74, 8, (0, 1, 2)), 2),
}, user=GLASS))

# ---- M08 Norm(5 面): 戦艦の行進曲。重い金管のオスティナート、軍楽のスネア ----
m08 = ['Cm', 'Cm', 'Ab', 'G', 'Cm', 'Fm', 'G', 'G7']
ost = 'o3c8. c16 c8 g8 c4 d+8 c8 '
SONGS.append(Song('M08', 120, {
    'FM0': 'L @0 v13 q2 o5c4. d8 d+4 g4 | f4 d+8 d8 c2 | d+4. f8 g4 o6c4 | o5b2 g2 | '
           'o5c4. d8 d+4 g4 | g+4 f4 d+4 c4 | d4 d+4 f4 g4 | g2 b2',
    'FM1': 'L @0 v11 ' + rep(ost, 2) + ' o2g+8. g+16 g+8 o3d+8 o2g+4 o3c8 o2g+8 o2g8. g16 g8 o3d8 o2g4 b8 g8 '
           + rep(ost, 1) + ' o3f8. f16 f8 o4c8 o3f4 g+8 f8 o2g8. g16 g8 o3d8 o2g4 b8 g8 o2g8. g16 g8 o3d8 o2g4 b8 g8',
    'FM2': 'L @9 v10 ' + pad(m08, 1, 60),
    'FM3': 'L @9 v9 ' + pad(m08, 2, 62),
    'FM4': 'L @14 v13 q4 ' + bass(m08, 1, (0, 12), 24),
    'RHY': 'L ' + rep('b4 s16 s16 s8 b4 s8. s16', 3) + ' PP05 b8 s16 s16 s16 s16 s16 s16 s16 s16 s16 s16 sc4 | '
           + rep('b4 s16 s16 s8 b4 s8. s16', 3) + ' PP05 s16 s16 s16 s16 s16 s16 s16 s16 s16 s16 s16 s16 bc4',
}, user=BRASS))

# ---- M09 Shadow of the Hour(6 面): G 短調、7/8(2+2+3)。PSG のノイズで時計の刻み ----
m09 = ['Gm', 'Cm', 'D', 'Gm', 'Eb', 'Cm', 'D', 'D7']
B7 = 84
def arp78(prog, lo):
    out = ''
    for sym in prog:
        t = tones(sym, lo)
        seq = [t[0], t[2], t[1], t[2], t[0] + 12, t[2], t[1]]
        out += ' '.join(nn(n, '8') for n in seq) + ' '
    return out
SONGS.append(Song('M09', 100, {
    'FM0': 'L @0 v12 q2 ' + arp78(m09, 67),
    'FM1': 'L @0 v10 q2 o5d4 c8 o4b-8 a4. | g4 a8 b-8 o5c4. | d4 f+8 a8 g4. | f+4 d8 o4b-8 g4. | '
           'o5g4 f8 d+8 d4. | c4 d+8 d8 c4. | o4b-4 a8 b-8 o5c4. | d2 r4.',
    'FM2': 'L @1 v8 ' + pad(m09, 1, 58, B7),
    'FM3': 'L @14 v12 q3 ' + ''.join(nn(tones(s, 31)[0], '4') + ' ' + nn(tones(s, 31)[0], '4') + ' ' + nn(tones(s, 31)[1], '4.') + ' ' for s in m09),
    'RHY': 'L ' + rep('b4 h4 s4.', 7) + ' b4 s8 s8 s8 s8 s8',
    'PSGC': 'L N1 @1 v11 q1 ' + rep('o5c8 o4c8 o4c8 o4c8 o5c8 o4c8 o4c8', 8),
}, bar=B7, user=HARP))

# ---- M10 Calibration(7 面): 冷たいミニマル。半音と三全音の機械的な列 ----
cal = 'o4c16 c+16 f+16 g16 c16 c+16 f+16 g16 o5c16 o4b16 f16 e16 c16 c+16 f+16 g16 '
SONGS.append(Song('M10', 140, {
    'FM0': 'L @0 v11 q2 ' + rep(cal, 8),
    'FM1': 'L @12 v10 q4 o5c2 f+2 | r1 | c+2 g2 | r1 | o4b2 f2 | r1 | c+2 g2 | o5c1',
    'FM3': 'L @13 v12 q3 ' + rep('o2c8 c8 c8 c8 c8 c8 c8 o1b8 ', 8),
    'RHY': 'L ' + rep('h16 h16 h16 h16 bh16 h16 h16 h16 h16 h16 h16 h16 sh16 h16 h16 h16', 8),
    'PSGA': 'L @1 v9 ' + rep('o6c16 r16 r8 f+16 r16 r8 r4 c+16 r16 g16 r16 ', 8),
}, user=GLASS))

# ---- M11 The Prototype(8 面): B 短調、タイトルの動機を短調で、合唱のパッド ----
# ★遊んでの指摘「1 オクターブ低くて OK。音を出すのがつらそう」で旋律とアルペジオを 1 オクターブ下げた
m11 = ['Bm', 'G', 'A', 'F#m', 'Bm', 'G', 'Em', 'F#', 'G', 'A', 'Bm', 'Bm', 'Em', 'F#', 'Bm', 'F#7']
SONGS.append(Song('M11', 152, {
    'FM0': 'L @0 v13 q2 o3b4 o4f+4 b4 o5c+4 | d2 c+4 o4b4 | a4. b8 o5c+4 e4 | c+1 | '
           'o3b4 o4f+4 b4 o5c+4 | d2 e4 f+4 | g4. f+8 e4 d4 | c+2 o4a+2 | '
           'o4b4 o5d4 g4 f+4 | e4 c+4 a4 g4 | f+2. d4 | b1 | g4 e4 o5c+4 o4b4 | a+4 o5c+4 e4 f+4 | d4. c+8 o4b4 f+4 | a+2 f+2',
    'FM1': 'L @1 v10 ' + pad(m11, 0, 54),
    'FM2': 'L @1 v10 ' + pad(m11, 1, 58),
    'FM3': 'L @1 v9 ' + pad(m11, 2, 62),
    'FM4': 'L @13 v13 q3 ' + bass(m11, 2, (0, 0, 12, 0, 0, 12, 0, 7), 12),
    'RHY': 'PP02 L ' + rep('bh8 h8 sh8 h8 bh8 bh8 sh8 h8', 7) + ' bc8 h8 sh8 h8 s16 s16 s16 s16 sc8 s8 | '
           + rep('bh8 h8 sh8 h8 bh8 bh8 sh8 h8', 7) + ' bc8 b8 s8 s8 s16 s16 s16 s16 sc4',
    'PSGA': 'L @1 v9 ' + arp(m11, 54, 6, (0, 1, 2, 3)),
}, user=TPT))

# ---- M12 Tolerance Check(中ボス、全面共通): 半音ずつ上がる低音 ----
crom = ' '.join(nn(40 + i // 2, '8') for i in range(16))       # e2 から 1 拍ごとに半音上がる(2 小節)
SONGS.append(Song('M12', 160, {
    'FM0': 'L @0 v12 q2 o5e4 r8 e8 f4 r8 f8 | f+4 r8 f+8 g4 r8 g8 | g+4 r8 g+8 a4 r8 a8 | a+4 a+8 b8 o6c4 c+4',
    'FM1': 'L @15 v10 q3 ' + ' '.join(nn(52 + i // 2, '8') for i in range(32)),
    'FM3': 'L @13 v13 q2 ' + crom + ' ' + ' '.join(nn(48 + i // 2, '8') for i in range(16)),
    'RHY': 'PP01 L ' + rep('bh8 h8 sh8 bh8 h8 bh8 sh8 h8', 3) + ' bc8 s8 s8 s8 s16 s16 s16 s16 sc4',
    'PSGA': 'L @1 v8 ' + ' '.join(nn(64 + i // 4, '16') for i in range(64)),
}, user=LEAD))

# ---- M13 Out of Tolerance(ボス、全面共通。7 面 LIBRA も): 金管、Criterion の動機の低音、4 小節ごとにオケヒ ----
m13 = ['Gm', 'Gm', 'Eb', 'D', 'Gm', 'Gm', 'Cm', 'D', 'Eb', 'F', 'Gm', 'Gm', 'Eb', 'F', 'D', 'D7']
def cri(prog, oct=2):
    """Criterion の動機: 同じ音を 8 分で 7 つ刻み、最後に半音下"""
    out = ''
    for s in prog:
        r = oct * 12 + chord(s)[0]
        out += ' '.join(nn(r, '8') for _ in range(7)) + ' ' + nn(r - 1, '8') + ' '
    return out
SONGS.append(Song('M13', 156, {
    'FM0': 'L @0 v13 q2 o5g4. a8 a+4 o6d4 | c4 o5a+8 a8 g2 | g4. f8 d+4 g4 | f+2 d2 | '
           'g4. a8 a+4 o6d4 | d+4 d8 c8 o5a+4 g4 | o6c4 d+4 d4 c4 | o5a2 f+2 | '
           'g4 d4 g4 a4 | a+2 a4 g4 | o5d4 a4 o6d4 e4 | f2 d2 | d+4 d4 c4 o5a+4 | a2 o6c2 | o5a4 f+4 d4 f+4 | a2 o6c2',
    'FM1': 'L @9 v10 ' + pad(m13, 1, 58),
    'FM2': 'L @9 v10 ' + pad(m13, 2, 62),
    'FM3': 'L @14 v13 q3 ' + cri(m13),
    'FM4': 'L @7 v9 q2 r1 r1 r1 r1 r1 r1 r1 r1 o4g4 d4 g4 a4 | a+1 | o4d4 a4 o5d4 e4 | f1 | r1 r1 r1 r1',
    'RHY': 'L ' + ' '.join(['PP02 ' + rep('bh8 h8 sh8 h8', 7) + ' bc8 s8 s16 s16 s8'] * 4),
    'PSGA': 'L @1 v8 ' + arp(m13, 67, 6, (0, 1, 2, 1)),
}, user=BRASS))

# ---- M14 Prototype Zero(ラスボス 3 楽章) ----
m14a = ['Bm', 'G', 'Em', 'F#', 'Bm', 'G', 'C', 'F#']
SONGS.append(Song('M14A', 80, {
    'FM0': 'PP03 L @0 v12 o4b2 o5c+4 d4 | d2 o4b2 | o4g2 a4 b4 | a+1 | o5d2 e4 f+4 | g2 f+4 e4 | e2 d4 c4 | c+1',
    'FM1': 'L @0 v10 ' + pad(m14a, 1, 58),
    'FM2': 'L @0 v10 ' + pad(m14a, 2, 62),
    'FM3': 'L @8 v12 ' + pad(m14a, 0, 38),
    'RHY': 'L ' + rep('b1', 7) + ' b2 c2',
    'PSGA': 'L @4 v7 ' + arp(m14a, 71, 24, (0, 1, 2, 1)),
}, user=ORGAN))

B74 = 168          # 7/4
m14b = ['Bm', 'G', 'Bm', 'A', 'Em', 'F#', 'Bm', 'F#']
SONGS.append(Song('M14B', 152, {
    'FM0': 'PP02 L @0 v13 q2 o5b4 b4 a8 b8 o6d4 c+4 o5b4 a4 | g4 a4 b4 o6d8 e8 f+4 e4 d4 | '
           'o5b4 b4 a8 b8 o6d4 e4 f+4 e4 | e4 d4 c+4 o5a4 b4 a4 g4 | '
           'g4 a4 b4 o6e4 d4 c+4 o5b4 | a+4 o6c+4 e4 f+4 e4 d4 c+4 | d4 c+4 o5b4 a4 g4 f+4 e4 | f+2. a+2 o6c+4 o5a+4',
    'FM1': 'L @0 v10 ' + pad(m14b, 1, 58, B74),
    'FM2': 'L @0 v9 ' + pad(m14b, 2, 62, B74),
    'FM3': 'L @13 v13 q2 ' + bass(m14b, 2, (0, 0, 12, 0, 7, 0, 12), 24, B74),
    'RHY': 'L ' + rep('bh4 sh4 bh4 bh8 bh8 sh4 bh4 sh4', 7) + ' bc4 s4 s4 s8 s8 s4 s4 sc4',
    'PSGA': 'L @1 v8 ' + arp(m14b, 66, 12, (0, 1, 2, 3, 2, 1, 0), B74),
}, bar=B74, user=BRASS))

m14c = ['D', 'A', 'Bm', 'G', 'D', 'A', 'G', 'A', 'Bm', 'G', 'D', 'A', 'G', 'A', 'D', 'D']

def m14c_top(prog):
    """FM5: 2 小節ひと組。1 小節目は弦で和音の 3 度を旋律の上で伸ばし、2 小節目は弦の半分のあと金管が和音を 8 分で駆け上がる"""
    out = []
    for a, b in zip(prog[0::2], prog[1::2]):
        up = tones(b, 67)
        out.append('@1 v12 ' + nn(tones(a, 74)[1], '1'))
        out.append('@1 v12 ' + nn(tones(b, 74)[1], '2') + ' @7 v12 ' + ' '.join(nn(n, '8') for n in (up[0], up[1], up[2], up[0] + 12)))
    return ' | '.join(out)

# ★遊んでの指摘「音程高すぎ」で旋律を 1 オクターブ下げ、音色をトランペット(TPT)に。「もう少し豪華に」で 6 本目(m14c_top)と
#   ドラムを足した(聴き比べの B3)
SONGS.append(Song('M14C', 168, {
    'FM0': 'PP01 L @0 v14 q2 o3d4 a4 o4d4 e4 | a2 g4 f+4 | f+4. e8 d4 c+4 | d2 r2 | '
           'o3d4 a4 o4d4 e4 | a2 b4 o5c+4 | d4. c+8 o4b4 a4 | a2 r2 | '
           'o4b4 a4 f+4 d4 | g2 b4 o5d4 | f+4. e8 d4 c+4 | e2 r2 | o4b4 o5d4 g4 f+4 | e2 c+4 e4 | d4 a4 o6d4 e4 | a1',
    'FM1': 'L @1 v10 ' + pad(m14c, 1, 60),
    'FM2': 'L @1 v10 ' + pad(m14c, 2, 62),
    'FM3': 'L @7 v10 q2 ' + stab(m14c, 66, (0,)),
    'FM4': 'L @13 v13 q2 ' + bass(m14c, 2, (0, 0, 12, 0), 6),
    'RHY': 'L ' + ' '.join([p + ' bc8 h8 sh8 h8 ' + rep('bh8 h8 sh8 h8', 6) + ' bc8 s16 s16 s16 s16 s16 s16'
                            for p in ('PP01', '', 'PP01', '')]),          # 4 小節ごとに頭のシンバル・終わりのスネアの連打。オケヒは 1・3 段落
    'FM5': 'L ' + m14c_top(m14c),
    'PSGA': 'L @1 v9 ' + arp(m14c, 59, 6, (0, 1, 2, 3)),
}, user=TPT))

# ---- M15 Escape Velocity(脱出): E 長調 180 ----
m15 = ['E', 'B', 'C#m', 'A', 'E', 'B', 'A', 'B7']
SONGS.append(Song('M15', 180, {
    'FM0': 'L @0 v13 q2 o5e8 f+8 g+8 b8 o6e4 d+8 c+8 | o5b2 f+4 b4 | c+8 d+8 e8 g+8 o6c+4 o5b8 a8 | a2 e4 c+4 | '
           'e8 f+8 g+8 b8 o6e4 f+8 g+8 | f+2 d+4 o5b4 | o6c+4 o5a4 o6e4 c+4 | d+2 o5b2',
    'FM1': 'L @15 v10 q3 ' + stab(m15, 64, (0,)),
    'FM2': 'L @1 v9 ' + pad(m15, 1, 60),
    'FM3': 'L @13 v13 q2 ' + bass(m15, 2, (0, 12, 0, 12), 6),
    'RHY': 'L PP06 ' + rep('bh8 bh8 sh8 bh8 h8 bh8 sh8 h8', 3) + ' bc8 s8 s8 s8 s16 s16 s16 s16 sc4 PP06 '
           + rep('bh8 bh8 sh8 bh8 h8 bh8 sh8 h8', 3) + ' bc8 s16 s16 s8 s16 s16 s16 s16 s16 s16 s16 s16 sc8',
    'PSGA': 'L @1 v9 ' + arp(m15, 68, 6, (0, 1, 2, 3, 2, 1, 2, 3)),
}, user=LEAD))

# ---- M16 Beyond Measure(エンディング): D 長調 76、Infinitas の動機をゆっくり。楽器が減っていく(g_snd_mute) ----
m16 = ['D', 'A/C#', 'Bm', 'G', 'D', 'A', 'G', 'A', 'Bm', 'G', 'D', 'A', 'G', 'A', 'D', 'D']
m16c = ['D', 'A', 'Bm', 'G', 'D', 'A', 'G', 'A', 'Bm', 'G', 'D', 'A', 'G', 'A', 'D', 'D']
SONGS.append(Song('M16', 76, {
    'FM0': 'L @0 v12 o4d2 a2 | o5d2 e2 | f+2. e4 | d1 | o4d2 a2 | o5d2 e4 f+4 | g2 f+4 e4 | e1 | '
           'f+2 e4 d4 | d2 o4b2 | a2 o5d4 e4 | e1 | d2 e4 f+4 | g2 a2 | a1& | a1',
    'FM1': 'L @1 v9 ' + pad(m16c, 1, 60),
    'FM2': 'L @1 v9 ' + pad(m16c, 2, 62),
    'FM3': 'L @14 v11 ' + pad(m16c, 0, 38),
    'PSGA': 'L @7 v8 ' + arp(m16c, 66, 12, (0, 1, 2, 3, 2, 1, 2, 1)),
    'PSGB': 'L @7 v5 ' + rot(arp(m16c, 66, 12, (0, 1, 2, 3, 2, 1, 2, 1)), 1),
}, user=FLUTE))

# ---- M17 Measured(ゲームオーバー、約 8 秒): 下降する和音、最後に時計のチック 1 回 ----
SONGS.append(Song('M17', 60, {
    'FM0': 'PP03 @0 v12 o5a2 f2 | e2 d2',
    'FM1': '@0 v11 o5f2 d2 | c+2 o4a2',
    'FM2': '@0 v11 o5d2 o4a+2 | a2 f2',
    'FM3': '@14 v12 ' + cri(['Dm', 'A']),                    # Criterion の動機(同じ音を 7 つ刻んで半音下)で測り終える
    'PSGC': 'N1 @1 v12 q1 r1 r2. o5c4',
}, loop=False, user=STRINGS))

# ---- J01 Clear(4 秒、飛び続けながら): 短い上行のファンファーレ ----
SONGS.append(Song('J01', 120, {
    'FM0': 'PP01 @0 v14 q2 o4a8 o5e8 a8 b8 o6c+4 e4 | a2 r2',
    'FM1': '@1 v11 o5c+2 e2 | e2 r2',
    'FM2': '@1 v11 o4a2 o5c+2 | c+2 r2',
    'FM3': '@14 v13 o2a4 a4 o3e4 e4 | a2 r2',
    'RHY': 'b8 h8 s8 h8 b8 s16 s16 s8 s8 | bc2 r2',
}, loop=False, user=BRASS))

# ---- J02 Warning(3 秒): PSG のサイレン → ボス曲へ ----
SONGS.append(Song('J02', 120, {
    'PSGA': '@0 v13 q1 ' + rep('o5a8 e8', 6) + ' r4 r4',
    'PSGB': '@0 v9 q1 ' + rep('o4a8 e8', 6) + ' r4 r4',
    'FM3': '@13 v12 o2a1 a2 r2',
}, next='M13', loop=False, user=BRASS, free=True))

# ---- M18 Record of the Unmeasured(ネームエントリー): F 長調 116、オルゴール風(PSG の分散和音 + ビブラフォン)。
#      少し切ないが前向き。文字盤のスナップ(8 分 = 約 0.26 秒)と拍がそろう ----
m18 = ['F', 'Dm', 'Bb', 'C', 'F', 'Am', 'Bb', 'C']
SONGS.append(Song('M18', 116, {
    'FM0': 'L @0 v12 o5c4 f4 a4 g8 f8 | f2 d4 e4 | f4 g4 a4 a+8 a8 | g2. r4 | '
           'a4 o6c4 o5a4 g8 f8 | e2 c4 e4 | d4 f4 a+4 a8 g8 | f2 r4 c4',
    'FM1': 'L @1 v8 ' + pad(m18, 1, 60),
    'FM2': 'L @1 v8 ' + pad(m18, 2, 62),
    'FM3': 'L @14 v10 ' + pad(m18, 0, 41),
    'PSGA': 'L @7 v8 ' + arp(m18, 72, 12, (0, 1, 2, 3, 2, 1, 2, 1)),
    'PSGB': 'L @7 v5 ' + rot(arp(m18, 72, 12, (0, 1, 2, 3, 2, 1, 2, 1)), 1),
}, user=VIBE))

# ---- M08C(5 面の撃沈の後の哀歌、約 13 秒): 行進曲の旋律を遅く、弦で。くり返さない ----
m08c = ['Cm', 'Ab', 'Fm', 'G', 'Cm', 'Fm', 'G', 'Cm']
SONGS.append(Song('M08C', 72, {
    'FM0': '@0 v11 o5c2. d4 | d+2 c2 | f2 d+4 d4 | d1 | c2. d4 | d+2 f2 | d2 o4b2 | o5c1',
    'FM1': '@9 v8 ' + pad(m08c, 1, 58),
    'FM2': '@9 v8 ' + pad(m08c, 2, 62),
    'FM3': '@14 v10 ' + pad(m08c, 0, 36),
}, loop=False, user=STRINGS))


# ---- M10X Calibration Overdrive(8 面の空中戦。遊んでの頼み「7 面の空中戦の曲の豪華版に」): M10 の刻み・鐘・ベースはそのまま、
#      弦のパッド(鐘の和音の 5 度上)・オーボエの対旋律(半音で上がる長い音)・4 小節ごとのシンバルとスネアの連打を足す ----
SONGS.append(Song('M10X', 140, {
    'FM0': 'L @0 v11 q2 ' + rep(cal, 8),
    'FM1': 'L @12 v10 q4 o5c2 f+2 | r1 | c+2 g2 | r1 | o4b2 f2 | r1 | c+2 g2 | o5c1',
    'FM2': 'L @1 v8 o4g1 | g1 | g+1 | g+1 | f+1 | f+1 | g+1 | g1',
    'FM3': 'L @13 v12 q3 ' + rep('o2c8 c8 c8 c8 c8 c8 c8 o1b8 ', 8),
    'FM4': 'L @6 v11 o5c2. c+4 | f+1 | c+2. d4 | g1 | o4b2. o5c4 | f1 | c+2 d+4 e4 | f+1',
    'RHY': 'L ' + rep('h16 h16 h16 h16 bh16 h16 h16 h16 h16 h16 h16 h16 sh16 h16 h16 h16', 3)
           + ' bc16 h16 h16 h16 bh16 h16 s16 s16 b16 s16 s16 s16 s16 s16 s16 s16 | '
           + rep('h16 h16 h16 h16 bh16 h16 h16 h16 h16 h16 h16 h16 sh16 h16 h16 h16', 3)
           + ' bc16 h16 h16 h16 bh16 h16 s16 s16 b16 s16 s16 s16 s16 s16 s16 s16',
    'PSGA': 'L @1 v9 ' + rep('o6c16 r16 r8 f+16 r16 r8 r4 c+16 r16 g16 r16 ', 8),
}, user=GLASS))

# ================================================================ PSG で厚みを足す(全曲)
# ★遊んでの指摘(ドラムがポコポコ、メロディーに厚みがない)。M01 で 4 案を聴き比べて決めた形を、旋律(FM0)とリズム(RHY)から作る:
#   PSG B = 旋律を同じ高さで重ねる(D2 = 周期 +2 で少し低く、FM とうならせる。音量は 4 段下げる)。やまびこのアルペジオ(B)は替わる
#   PSG C = ノイズのスネア(v13、8 刻み)と閉じたハイハット(v9、3 刻み)。バスドラム・シンバル・タムは FM のまま
#   PSG C は効果音と取り合う(鳴っている間は曲の側が抜ける。了承済み)。PSG だけの曲(J02)・C のノイズを自分で使う曲は触らない
import re as _re

def _psg_double(mml, env=0, drop=4, oct=0, tune=False):
    """env: PSG のエンベロープ(snd.c の env_rate。減衰する FM 音色に合わせる)、drop: 音量を下げる段、oct: 重ねる高さ(オクターブ)、
    tune: デチューンを高さで変える(周期に足すので高い音ほどずれが大きく、o6 では 4 分の 1 音外れて聞こえた。o4 以下 2、o5 1、o6 以上 0。
    ★聴き比べで、ほかの曲は D2 のままがよいとなったので、高い旋律の曲だけ)"""
    s = _re.sub(r'P\w+', '', mml)                 # PCM の合図は FM0 の側だけ
    s = _re.sub(r'T\d+', '', s)                   # テンポも
    s = _re.sub(r'@\d+', '@%d' % env, s)
    s = _re.sub(r'v(\d+)', lambda m: 'v%d' % max(0, int(m.group(1)) - drop), s)
    if oct:
        s = 'o%d ' % (4 + oct) + _re.sub(r'o(\d)', lambda m: 'o%d' % (int(m.group(1)) + oct), s)
    if tune:
        s = _re.sub(r'o(\d)', lambda m: 'o%s D%d' % (m.group(1), 2 if int(m.group(1)) <= 4 else 1 if m.group(1) == '5' else 0), s)
    return 'D2 @%d ' % env + s

# 曲ごとの重ね方(無い曲は既定: 減衰なし・4 段下げ・同じ高さ)。聴き比べで決めた:
#   M09 = ハープシコードはすぐ減衰するので、PSG も 3 フレームに 1 段ずつ下げる(減衰なしでは PSG だけが鳴り残った)
#   M10 = 旋律が低く小さい(o4・v11)ので、下げるのは 1 段だけ(4 段では埋もれて変わって聞こえなかった)
PSG_DOUBLE = {'M09': {'env': 6}, 'M10': {'drop': 1}, 'M10X': {'drop': 1}, 'M11': {'tune': True}, 'M14C': {'tune': True}}

_RT = _re.compile(r'\s+|\||([bstch]+)(%\d+|\d+)?(\.*)|(r)(%\d+|\d+)?(\.*)|(L)|v(\d+)|P(\w+)|T(\d+)')
def _ticks(num, dots):
    t = int(num[1:]) if num and num.startswith('%') else (96 // int(num) if num else 24)
    a = t
    for _ in range(dots):
        a //= 2; t += a
    return t

def _psg_drums(rhy):
    out, i, hits = [], 0, 0
    while i < len(rhy):
        m = _RT.match(rhy, i)
        if not m or m.end() == i:
            raise ValueError('リズムが読めない: ' + rhy[i:i + 20])
        i = m.end(); g = m.groups()
        if g[0]:
            t = _ticks(g[1], len(g[2]))
            if 's' in g[0]: out.append('q%d v13 o4e%%%d' % (max(0, t - 8), t)); hits += 1
            elif 'h' in g[0]: out.append('q%d v9 o5d+%%%d' % (max(0, t - 3), t)); hits += 1
            else: out.append('r%%%d' % t)
        elif g[3]: out.append('r%%%d' % _ticks(g[4], len(g[5])))
        elif g[6]: out.append('L')
    return 'N1 @1 ' + ' '.join(out) if hits else None

for _s in SONGS:
    if _s.id == 'M01' or 'FM0' not in _s.ch or not _s.ch['FM0'].strip():
        continue                                   # M01 は手で書いた / J02 は PSG だけの曲
    _s.ch['PSGB'] = _psg_double(_s.ch['FM0'], **PSG_DOUBLE.get(_s.id, {}))
    if 'RHY' in _s.ch and 'PSGC' not in _s.ch:
        _d = _psg_drums(_s.ch['RHY'])
        if _d:
            _s.ch['PSGC'] = _d
