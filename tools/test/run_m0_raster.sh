#!/bin/sh
# M0-2 ラスター検証: 割込み方式 × 陽炎帯 × 帯の本数 を総当たりし、CPU 残量と遅れを記録する。
# 環境変数: BANDS(帯の本数の列) HAZES(陽炎 0=無 1=R#26+27 2=R#27のみ) RBIAS(割込み補正)
# 各設定で時間を止めたスクリーンショットも撮り、check_raster.py で帯の境界・横位置・陽炎を照合する。
#   usage: tools/test/run_m0_raster.sh <作業ディレクトリ> [ROM] [NOI]
set -e
OUT=${1:?usage: $0 <workdir> [rom] [noi]}
ROM=${2:-build/m0_raster.rom}
NOI=${3:-build/m0_raster.noi}
MACHINE=${MACHINE:-My_FS-A1ST_512K}
BANDS=${BANDS:-"2 4 6 8 10 12 14 16"}
mkdir -p "$OUT"
sym() { awk -v s="$1" '$2==s{print $3}' "$NOI"; }
CFG=$(sym _g_cfg); ST=$(sym _g_m2); BIAS=$(sym _g_ras_bias)
T="$OUT/m0_raster.tcl"
cat > "$T" <<TCL
set throttle on
set f [open "$OUT/m0_raster.log" w]
proc stat {tag} {
  set b [debug read_block memory $ST 21]
  binary scan \$b susususususucucucusuiu b0 b1 im il lt t md n rx fr sum
  set avg [expr {\$fr ? \$sum / \$fr : 0}]
  set base [expr {\$md ? \$b1 : \$b0}]
  puts \$::f "\$tag mode=\$md ramx=\$rx frames=\$fr avg=\$avg base=\$base pct=[expr {\$base ? 100*\$avg/\$base : 0}] min=\$im late=\$lt t=\$t n=\$n bias=[debug read memory $BIAS]"
  flush \$::f
}
proc cfg {b h m z} {
  debug write memory $CFG \$b; debug write memory [expr {$CFG+1}] \$h
  debug write memory [expr {$CFG+2}] \$m; debug write memory [expr {$CFG+3}] \$z
}
proc shot {tag} {
  binary scan [debug read_block memory [expr {$ST + 10}] 2] su t
  puts \$::f "SHOT \$tag t=\$t"; flush \$::f
  screenshot -raw "$OUT/\$tag.png"
}
set t 12
after time 11 { debug write memory $BIAS ${RBIAS:-4} }
foreach m {0 1} { foreach h {${HAZES:-0 2}} { foreach b {$BANDS} {
  set tag "m\${m}_h\${h}_b\${b}"
  after time \$t [list cfg \$b \$h \$m 0]
  after time [expr {\$t+2.0}] [list stat \$tag]
  after time [expr {\$t+2.0}] [list cfg \$b \$h \$m 1]
  after time [expr {\$t+2.4}] [list shot \$tag]
  set t [expr {\$t+2.5}]
}}}
after time \$t { close \$::f ; exit }
TCL
timeout 300 openmsx -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 -script "$T" >/dev/null 2>&1 || true
python3 tools/test/check_raster.py "$OUT"
