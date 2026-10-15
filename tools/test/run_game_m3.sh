#!/bin/sh
# M3 の検証(GAME.ROM): ROTOR・GHOST・Pawn・LEGION・PLUMB が「正しく描けているか」を VRAM で確かめる。
#   無敵・連射で 1 面を流す(ボス戦の間は自機をボスの高さへ置く)。throttle off。
#   1. city_ring: 都市前半の途中で、表示リングの見えている列の「歩道より上」(s = 0..149)が、地図と短冊
#      (ページ 3)から作れるはずの絵と 1 バイトも違わない(列の流し込み・描き替え・GHOST の消し残しや
#      迷子の書き込みを捕まえる。M3 でコマンドのレジスタを省いて画面全体が崩れたのを見逃した反省)
#   2. legion: LEGION と戦っている最中、積んであるマス(g_lg_on)がリングに Pawn の絵で描かれていて、
#      120 マス以上積み上がっている
#   3. rotor: 都市後半で ROTOR が作った PLUMB の 10 枚(ページ 2)が tools/gen_boss.py の答えと一致(PLUMB と戦い始めたとき)
#   4. no_late: 遅れたフレーム 0(起動から PLUMB を倒して上昇するまで)
#   5. gq_drop: GHOST の待ち行列が溢れて捨てたコマンドが 0(捨てると描き戻しが抜けて絵が崩れる)
#   usage: tools/test/run_game_m3.sh <作業ディレクトリ>
set -e
OUT=${1:?usage: $0 <workdir>}
ROM=GAME.ROM
NOI=build/game.noi
MACHINE=${MACHINE:-My_FS-A1ST_512K}
mkdir -p "$OUT"; rm -f "$OUT"/rotor.vram "$OUT"/legion.vram "$OUT"/city.vram
sym() { awk -v s="$1" '$2==s{print $3}' "$NOI"; }
ST=$(sym _g_stat); GQD=$(sym _g_gq_drop); PL=$(sym _g_pl); SEC=$(sym _g_sec); BS=$(sym _g_boss_state); INV=$(sym _g_dbg_invul)
WX=$(sym _g_wx); W0=$(sym _g_w0); LGON=$(sym _g_lg_on); LGX=$(sym _g_lg_x0); SCAM=$(sym _g_scam)
PCMOFF=$(awk '$2=="_g_pcm_off"{print $3}' "$NOI")   # ★PCM は切る(エンジンの時間を見る。PCM は run_game_m7.sh)
T="$OUT/game_m3.tcl"
cat > "$T" <<TCL
set throttle off
after time 6 { debug write memory $PCMOFF 1 }
set f [open "$OUT/game_m3.log" w]
proc rd16 {a} { binary scan [debug read_block memory \$a 2] su v; return \$v }
proc rd {a} { return [debug read memory \$a] }
proc dump {name} {
  set g [open "$OUT/\$name.vram" w]; fconfigure \$g -translation binary
  puts -nonewline \$g [debug read_block VRAM 0 0x20000]; close \$g
  binary scan [debug read_block memory $WX 2] su wx
  binary scan [debug read_block memory $W0 2] su w0
  binary scan [debug read_block memory $SCAM 2] s sc
  binary scan [debug read_block memory $LGON 52] su26 lg
  binary scan [debug read_block memory $LGX 2] s lx
  set h [open "$OUT/\$name.txt" w]; puts \$h "wx=\$wx w0=\$w0 scam=\$sc sec=[rd $SEC] bs=[rd $BS] lx=\$lx lg=\$lg"; close \$h
}
set ::legion_done 0
proc watch {} {
  set s [rd $SEC]
  if {\$s == 3} { debug write_block memory [expr {$PL+2}] [binary format s 1280] }
  if {\$s == 5} { debug write_block memory [expr {$PL+2}] [binary format s 1248] }
  if {\$s == 3 && [rd $BS] == 2 && !\$::legion_done && [rd16 [expr {$LGON + 0}]] != 0} {
    incr ::legion_wait
    if {\$::legion_wait > 8} { dump legion; set ::legion_done 1 }
  }
  if {\$s == 5 && [rd $BS] == 2 && !\$::rotor_done} { dump rotor; set ::rotor_done 1 }   ;# ★PLUMB は絵ができてから降りてくる(作る速さは重さしだいなので、決まった時刻では照合しない)
  if {\$s == 6} { puts \$::f "late=[rd16 [expr {$ST+4}]] drop=[rd16 $GQD]"; close \$::f; exit }
  after time 0.25 watch
}
set ::legion_wait 0
set ::rotor_done 0
after time 16.6 { keymatrixdown 8 0x01 }
after time 16.8 { keymatrixup 8 0x01 }
after time 19.0 { debug write memory $INV 1; keymatrixdown 8 0x01; watch }
# ★1 面の空中戦を 512 → 1024 ドットに延ばした(M7 の後)ので、都市の時刻は 8.5 秒遅れる
after time 54 { dump city }
after time 200 { puts \$::f "timeout"; close \$::f; exit }
TCL
timeout 600 openmsx -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 -script "$T" >/dev/null 2>&1 || true
python3 - "$OUT" <<'PY'
import sys, os
out = sys.argv[1]
fails = 0
def check(name, ok, detail):
    global fails
    print(f"CHECK {name} {'OK' if ok else 'NG'} {detail}")
    if not ok: fails += 1
def info(name):
    d = {}
    for kv in open(os.path.join(out, name + '.txt')).read().split(' lg=')[0].split():
        k, v = kv.split('='); d[k] = int(v)
    d['lg'] = [int(x) for x in open(os.path.join(out, name + '.txt')).read().split(' lg=')[1].split()]
    return d
cmap = open('build/city_map.bin', 'rb').read()
# 1. 都市のリング
v = open(os.path.join(out, 'city.vram'), 'rb').read(); d = info('city')
wx, w0 = d['wx'], d['w0']
bad = 0; cols = 0
for W in range((wx + 8 + 7) >> 3, (wx + 248) >> 3):
    if 8 * W + 8 > wx + 248: break
    m = W - w0
    k = cmap[m] if 0 <= m < len(cmap) else 0
    c = W & 63; page = c >> 5; xb = (c & 31) * 4
    cols += 1
    for s in range(150):
        r = (188 + s) & 255
        got = v[page * 0x8000 + r * 128 + xb: page * 0x8000 + r * 128 + xb + 4]
        kk = (27, 28, 29, 30, 31, 27, 28, 29)[W & 7] if 16 <= s < 40 and k <= 26 else k   # ★遠景の行は遠景の短冊(bank/far.c の FAR_CITY)
        exp = v[0x18000 + s * 128 + kk * 4: 0x18000 + s * 128 + kk * 4 + 4]
        if got != exp: bad += 1
check('city_ring', d['scam'] == 0 and bad == 0 and cols >= 28, f"列={cols} 違う 4 バイト={bad} (wx={wx} scam={d['scam']} 区間={d['sec']})")
# 2. LEGION
if os.path.exists(os.path.join(out, 'legion.vram')):
    v = open(os.path.join(out, 'legion.vram'), 'rb').read(); d = info('legion')
    pawn = [bytes([0x1E, 0xE1]), bytes([0xED, 0xDE]), bytes([0xEE, 0xEE]), bytes([0xE1, 0x1E])]
    on = 0; bad = 0
    for j in range(26):
        for i in range(12):
            if not (d['lg'][j] >> (11 - i)) & 1: continue
            on += 1
            rx = (d['wx'] + d['lx'] + 4 * i) & 511
            for q in range(4):
                ry = (54 + 4 * j + q + 188) & 255
                a = (0x8000 if rx >= 256 else 0) + ry * 128 + (rx & 255) // 2
                if v[a:a + 2] != pawn[q]: bad += 1
    check('legion', on >= 120 and bad == 0, f"積んだマス={on} 絵の違う行={bad} (状態={d['bs']})")
else:
    check('legion', False, "戦う場面に入らなかった")
# 3. ROTOR の 10 枚
v = open(os.path.join(out, 'rotor.vram'), 'rb').read()
ref = open('build/bob_ref.bin', 'rb').read()
got = bytearray()
for k in range(10):
    for r in range(64):
        a = 0x10000 + ((k >> 2) * 64 + r) * 128 + (k & 3) * 32
        got += v[a:a + 32]
diff = sum(1 for a, b in zip(got, ref) if a != b)
check('rotor', diff == 0, f"違うバイト={diff} / {len(ref)}")
# 4. 遅れ
log = open(os.path.join(out, 'game_m3.log')).read()
late = int(log.split('late=')[1].split()[0]) if 'late=' in log else -1
check('no_late', late == 0, f"遅れたフレーム={late}")
drop = int(log.split('drop=')[1].split()[0]) if 'drop=' in log else -1
check('gq_drop', drop == 0, f"GHOST の待ち行列から捨てたコマンド={drop}")
print('判定: ' + ('OK' if not fails else f'NG ({fails})'))
open(os.path.join(out, 'game_m3.result'), 'w').write('OK' if not fails else 'NG')
PY
grep -q OK "$OUT/game_m3.result"
