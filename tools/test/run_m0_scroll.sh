#!/bin/sh
# M0-1 スクロール検証の実行。
#   usage: tools/test/run_m0_scroll.sh <作業ディレクトリ>
# 作業ディレクトリ(セッションのスクラッチ推奨)に Tcl とスクリーンショット/ログを置く。
# openMSX は FS-A1ST(実 BIOS)定義で起動する(C-BIOS turboR はライン割込みが出ない)。
set -e
OUT=${1:?usage: $0 <workdir>}
ROM=build/m0_scroll.rom
MACHINE=${MACHINE:-My_FS-A1ST_512K}
mkdir -p "$OUT"
sym() { awk -v s="$1" '$2==s{print $3}' build/m0_scroll.noi; }
STAT=$(sym _g_m0); FRZ=$(sym _g_dbg_freeze); POS=$(sym _g_dbg_pos)
POSLIST="0 1 2 3 4 5 6 7 8 9 15 248 255 256 257 300 504 511 512 513 1000 4093 65530"
T="$OUT/m0_scroll.tcl"
cat > "$T" <<TCL
set throttle on
set f [open "$OUT/m0_scroll.log" w]
proc stat {tag} {
  set b [debug read_block memory $STAT 17]
  binary scan \$b sususususususs ib im il ms cl fr ps sp
  puts \$::f "\$tag base=\$ib min=\$im miss=\$ms cols=\$cl frame=\$fr pos=\$ps speed=\$sp"
  flush \$::f
}
proc setpos {p} {
  debug write memory $POS [expr {\$p & 255}]
  debug write memory [expr {$POS + 1}] [expr {(\$p >> 8) & 255}]
  debug write memory $FRZ 1
}
proc shot {p} { stat "frozen"; screenshot -raw "$OUT/m0s_\$p.png" }
# 0..27 秒: 速度プログラムを一巡(0.5→8 加速、急停止、逆走)させて取りこぼしを見る
after time 27 { stat "run" }
set t 28.0
foreach p {$POSLIST} {
  after time \$t [list setpos \$p]
  after time [expr {\$t + 1.0}] [list shot \$p]
  set t [expr {\$t + 1.2}]
}
after time [expr {\$t + 0.2}] { close \$::f ; exit }
TCL
timeout 120 openmsx -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 -script "$T" >/dev/null 2>&1 || true
cat "$OUT/m0_scroll.log"
ARGS=""
for p in $POSLIST; do ARGS="$ARGS $OUT/m0s_$p.png $p"; done
python3 tools/test/check_scroll.py $ARGS
