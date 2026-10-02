#!/bin/sh
# M1 の総合検証(GAME.ROM): 1 面の空中戦が 60Hz で遊べるか。
#   0. 起動ロゴとタイトルを経て SPACE で出撃(M2)
#   1. 無敵・連射・上下移動で約 18 秒: 遅れたフレーム 0、得点が入る、Quanta でゲージが進む
#   2. ゲージ: SPLIT を確定 → split=1 / RAY を確定 → ray=1 かつ split=0(排他)/ ∞ で LIMIT BREAK →
#      敵が消え、ray が 1 段下がる
#   3. 被弾: 残機 1 で敵弾を自機に当てる → 残機 0 → GAME OVER → A で起動ロゴから(残機 3・得点 0)
# 画面も 2 枚撮る(game_play.png / game_over.png)。
#   usage: tools/test/run_game_m1.sh <作業ディレクトリ>
set -e
OUT=${1:?usage: $0 <workdir>}
ROM=GAME.ROM
NOI=build/game.noi
MACHINE=${MACHINE:-My_FS-A1ST_512K}
mkdir -p "$OUT"
sym() { awk -v s="$1" '$2==s{print $3}' "$NOI"; }
ST=$(sym _g_stat); GA=$(sym _g_gauge); PL=$(sym _g_pl); EN=$(sym _g_enemy); ES=$(sym _g_eshot); INV=$(sym _g_dbg_invul)
T="$OUT/game_m1.tcl"
cat > "$T" <<TCL
set throttle on
set f [open "$OUT/game_m1.log" w]
set fails 0
proc rd16 {a} { binary scan [debug read_block memory \$a 2] su v; return \$v }
proc rd {a} { return [debug read memory \$a] }
proc wr {a v} { debug write memory \$a \$v }
proc check {name cond detail} {
  global f fails
  if {\$cond} { puts \$f "CHECK \$name OK \$detail" } else { puts \$f "CHECK \$name NG \$detail"; incr fails }
  flush \$f
}
proc key {row bit dur} { keymatrixdown \$row \$bit; after time \$dur [list keymatrixup \$row \$bit] }
proc gauge {} { binary scan [debug read_block memory $GA 10] cu10 g; return \$g }
proc enemies {} { set n 0; for {set i 0} {\$i < 16} {incr i} { if {[rd [expr {$EN + \$i*16}]]} { incr n } }; return \$n }
set q0 0
# ---- 0. 起動ロゴ → タイトル → SPACE で出撃 → 空中戦(M2 から。以降の時刻は M1 の版より 6 秒遅い) ----
after time 15.0 { key 8 0x01 0.1 }
# ---- 1. 無敵・連射・上下移動 ----
after time 17.9 { wr $INV 1; keymatrixdown 8 0x01 }
foreach t {19 21 23 25 27 29 31 33 35} { after time \$t [list key 8 0x20 0.6]; after time [expr {\$t+1}] [list key 8 0x40 0.6] }
after time 17.95 { check sortie_to_air [expr {[rd [expr {$ST+14}]] == 3 && [rd16 [expr {$ST+4}]] == 0}] "phase=[rd [expr {$ST+14}]] late=[rd16 [expr {$ST+4}]]" }
after time 18 { set ::fr0 [rd16 [expr {$ST+6}]]; set ::lt0 [rd16 [expr {$ST+4}]] }
after time 36 {
  set fr [expr {[rd16 [expr {$ST+6}]] - \$::fr0}]; set lt [expr {[rd16 [expr {$ST+4}]] - \$::lt0}]
  check 60fps [expr {\$lt == 0 && \$fr > 1000}] "frames=\$fr late=\$lt"
  set sc [rd16 [expr {$PL+9}]]
  check score [expr {\$sc > 0}] "score=\${sc}0"
  set g [gauge]
  check quanta [expr {[lindex \$g 0] != 0 || [lindex \$g 3] > 0}] "cursor=[lindex \$g 0] thrust=[lindex \$g 3]"
  screenshot -raw "$OUT/game_play.png"
}
# ---- 2. ゲージの確定 ----
after time 36.2 { wr $GA 6; wr [expr {$GA+1}] 1; wr [expr {$GA+2}] 0 }
after time 36.3 { key 5 0x20 0.1 }
after time 36.6 { set g [gauge]; check split [expr {[lindex \$g 7] == 1}] "split=[lindex \$g 7]" }
after time 36.8 { wr $GA 7; wr [expr {$GA+1}] 1; wr [expr {$GA+2}] 0 }
after time 36.9 { key 5 0x20 0.1 }
after time 37.2 { set g [gauge]; check ray_exclusive [expr {[lindex \$g 8] == 1 && [lindex \$g 7] == 0}] "ray=[lindex \$g 8] split=[lindex \$g 7]" }
# 敵を 1 機置いてから(画面の中央、Tick、編隊なし)
after time 37.4 {
  debug write_block memory $EN [binary format cucususususucucucucu 1 0 3200 1280 [expr {-24 & 0xFFFF}] 0 1280 0 255 1 0]
  set ::en0 [enemies]; wr $GA 0; wr [expr {$GA+1}] 1; wr [expr {$GA+2}] 0
}
after time 37.5 { key 5 0x20 0.05 }
after time 37.56 { set g [gauge]; check limit_break [expr {\$::en0 > 0 && [enemies] == 0 && [lindex \$g 8] == 0}] "敵 \$::en0 → [enemies] ray=[lindex \$g 8]" }
# ---- 3. 被弾 → GAME OVER → 再開 ----
after time 38 { keymatrixup 8 0x01; wr $INV 0; wr [expr {$PL+8}] 1 }
after time 38.2 {
  set x [rd16 $PL]; set y [rd16 [expr {$PL+2}]]
  set bx [expr {(\$x + 128) & 0xFFFF}]; set by [expr {(\$y + 128) & 0xFFFF}]
  debug write_block memory $ES [binary format sususus \$bx \$by 0 0]
  wr [expr {$ES+8}] 1
}
after time 38.4 { check hit [expr {[rd [expr {$PL+8}]] == 0 && [rd [expr {$PL+4}]] == 0}] "lives=[rd [expr {$PL+8}]] alive=[rd [expr {$PL+4}]]" }
after time 40.5 { check game_over [expr {[rd [expr {$ST+12}]] == 1}] "state=[rd [expr {$ST+12}]]"; screenshot -raw "$OUT/game_over.png" }
after time 40.6 { key 8 0x01 0.1 }
after time 41 { check restart [expr {[rd [expr {$ST+12}]] == 0 && [rd [expr {$ST+14}]] == 0 && [rd [expr {$PL+8}]] == 3 && [rd16 [expr {$PL+9}]] == 0}] "state=[rd [expr {$ST+12}]] phase=[rd [expr {$ST+14}]] lives=[rd [expr {$PL+8}]] score=[rd16 [expr {$PL+9}]]" }
after time 41.1 { if {\$fails} { puts \$f "判定: NG (\$fails)" } else { puts \$f "判定: OK" }; close \$f; exit }
TCL
timeout 240 openmsx -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 -script "$T" >/dev/null 2>&1 || true
cat "$OUT/game_m1.log"
grep -q "判定: OK" "$OUT/game_m1.log"
