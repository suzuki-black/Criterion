#!/bin/sh
# M0-3 PCM 方式の比較: 方式(なし/A/B/C) × 陽炎帯(なし/あり)ごとに、
#   - D/A ポート(0xA4)への書き込みの時刻と値(openMSX のウォッチポイント)
#   - CPU 残量(g_m3)
#   - 録音(WAV)
# を取り、check_pcm.py で間隔のばらつき・欠落・再生の伸びを集計する。
#   usage: tools/test/run_m0_pcm.sh <作業ディレクトリ>
set -e
OUT=${1:?usage: $0 <workdir>}
ROM=build/m0_pcm.rom
NOI=build/m0_pcm.noi
MACHINE=${MACHINE:-My_FS-A1ST_512K}
mkdir -p "$OUT"
sym() { awk -v s="$1" '$2==s{print $3}' "$NOI"; }
CFG=$(sym _g_cfg); ST=$(sym _g_m3); REARM=$(sym _g_ras_rearm)
T="$OUT/m0_pcm.tcl"
cat > "$T" <<TCL
set throttle on
set f [open "$OUT/m0_pcm.log" w]
set w ""
proc onwrite {} { puts -nonewline \$::w "[machine_info time] \$::wp_last_value\n" }
proc stat {tag} {
  set b [debug read_block memory $ST 15]
  binary scan \$b susususuiusucu base im lt fr sum pl md
  set avg [expr {\$fr ? \$sum / \$fr : 0}]
  binary scan [debug read_block memory $REARM 2] su ra
  puts \$::f "\$tag method=\$md frames=\$fr base=\$base pct=[expr {\$base ? 100*\$avg/\$base : 0}] late=\$lt plays=\$pl rearm=\$ra"
  flush \$::f
}
proc cfg {m h} { debug write memory $CFG \$m; debug write memory [expr {$CFG+1}] \$h }
proc start {tag} {
  set ::w [open "$OUT/\$tag.da" w]
  set ::wp [debug set_watchpoint write_io 0xA4 {} onwrite]
  record start -audioonly -prefix x "$OUT/\$tag.wav"
}
proc stop {tag} {
  record stop
  debug remove_watchpoint \$::wp
  close \$::w
  stat \$tag
}
set t 12
foreach m {0 1 2 3} { foreach h {0 1} {
  set tag "p\${m}_h\${h}"
  after time \$t [list cfg \$m \$h]
  after time [expr {\$t+0.6}] [list start \$tag]
  after time [expr {\$t+2.6}] [list stop \$tag]
  set t [expr {\$t+2.8}]
}}
after time \$t { close \$::f ; exit }
TCL
timeout 300 openmsx -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 -script "$T" >/dev/null 2>&1 || true
python3 tools/test/check_pcm.py "$OUT"
