#!/bin/sh
# M0-4 画面分割(SCREEN12 / SCREEN5)の境界の検証。
#   - 分割の行 n(48 / 64)× 境界での書き換えの順(0 / 1)× PCM 方式 C(なし / あり)で、時間を止めて撮る
#   - 比較の基準: 同じ時刻の「全面 SCREEN12」と「全面 SCREEN5」
#   - SCREEN12 の帯の横位置 yx = 0..7 を撮り、1 ドット単位で単純にずれるだけか(色化けしないか)を見る
# check_mode.py が行ごとに照合して、境界で乱れる行数を出す。
#   usage: tools/test/run_m0_mode.sh <作業ディレクトリ>
set -e
OUT=${1:?usage: $0 <workdir>}
ROM=build/m0_mode.rom
NOI=build/m0_mode.noi
MACHINE=${MACHINE:-My_FS-A1ST_512K}
mkdir -p "$OUT"
sym() { awk -v s="$1" '$2==s{print $3}' "$NOI"; }
CFG=$(sym _g_cfg); ST=$(sym _g_m4); REARM=$(sym _g_ras_rearm)
T="$OUT/m0_mode.tcl"
cat > "$T" <<TCL
set throttle on
set f [open "$OUT/m0_mode.log" w]
# g_cfg: split n order pcm freeze yx(2)
proc cfg {split n order pcm freeze yx} {
  debug write memory $CFG \$split
  debug write memory [expr {$CFG+1}] \$n
  debug write memory [expr {$CFG+2}] \$order
  debug write memory [expr {$CFG+3}] \$pcm
  debug write memory [expr {$CFG+5}] [expr {\$yx & 255}]
  debug write memory [expr {$CFG+6}] [expr {\$yx >> 8}]
  debug write memory [expr {$CFG+4}] \$freeze
}
proc shot {tag} {
  binary scan [debug read_block memory $ST 23] susususuiususucu base im lt fr sum t wx yx sy
  binary scan [debug read_block memory $REARM 2] su ra
  puts \$::f "\$tag late=\$lt frames=\$fr wx=\$wx yx=\$yx rearm=\$ra"
  flush \$::f
  screenshot -raw "$OUT/\$tag.png"
}
set t 12
proc at {dt cmd} { global t; after time \$t \$cmd; set t [expr {\$t + \$dt}] }
# 走らせてから止める(以後の全ショットは同じ時刻・同じ位置)
at 1.0 [list cfg 1 48 0 0 0 0]
at 0.5 [list cfg 1 48 0 0 1 0]
foreach n {48 64} {
  at 2.0 [list cfg 0 \$n 0 0 0 0]
  at 0.6 [list cfg 0 \$n 0 0 1 0]
  at 0.2 [list shot "ref_g4_n\$n"]
  at 0.4 [list cfg 2 \$n 0 0 1 0]
  at 0.2 [list shot "ref_g7_n\$n"]
  foreach order {0 1} { foreach pcm {0 1} {
    at 0.6 [list cfg 1 \$n \$order \$pcm 1 0]
    at 0.2 [list shot "split_n\${n}_o\${order}_p\${pcm}"]
  }}
}
at 2.0 [list cfg 2 48 0 0 0 0]
foreach x {0 1 2 3 4 5 6 7} {
  at 0.4 [list cfg 2 48 0 0 1 \$x]
  at 0.2 [list shot "yjk_x\$x"]
}
at 0.2 { close \$::f ; exit }
TCL
# ★作者の openMSX の設定(~/.openMSX/share/settings.xml)を汚さない: 写しで起動する(openMSX は終了時に設定を保存し、
#   音量などが次の起動へ持ち越される)。元が無ければ空の設定で起動する
cp "$HOME/.openMSX/share/settings.xml" "$OUT/settings.xml" 2>/dev/null || printf "<!DOCTYPE settings SYSTEM 'settings.dtd'>\n<settings>\n</settings>\n" > "$OUT/settings.xml"
timeout 300 openmsx -setting "$OUT/settings.xml" -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 -script "$T" >/dev/null 2>&1 || true
python3 tools/test/check_mode.py "$OUT"
