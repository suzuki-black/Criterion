#!/bin/sh
# M5 の総合検証(GAME.ROM): 起動から 6 面の終わりまで(1〜4 面は通るだけ。細かい確かめは run_game_m4.sh)。
#   無敵・連射・上下移動で流す。ボス戦の間は自機をボスの高さ(g_boss_aim があればその弱点の高さ)へ置く。
#   1. 区間が順に進む: 2 面(7)から 6 面の終わり(51)まで
#   2. 5・6 面のボスを倒して進む(体力が 0 になった): HANGAR DECK・GAUGE BLOCK・HOURGLASS・GNOMON
#   3. 5 面の機関部: 天井と床のプレスの R#23 の項目が出る(帯の縦スクロール)
#   4. 6 面の画面分割: 帰線の 244 行で SCREEN 12(R#0 = 0x1E)、48 行で SCREEN 5 へ戻す境界(RAS_MARK)の項目。
#      ページ 0 と 2 の 0..47 行が YJK の絵(build/yjk_even.bin / yjk_odd.bin)と 1 バイトも違わない
#   5. 暗転なし・遅れたフレーム 0(起動から 6 面の終わりまで)
# throttle off で流す(時刻はエミュレータの時刻)。画面を数枚撮る(m5_*.png)。
#   usage: tools/test/run_game_m5.sh <作業ディレクトリ>
set -e
OUT=${1:?usage: $0 <workdir>}
ROM=GAME.ROM
NOI=build/game.noi
MACHINE=${MACHINE:-My_FS-A1ST_512K}
mkdir -p "$OUT"
sym() { awk -v s="$1" '$2==s{print $3}' "$NOI"; }
ST=$(sym _g_stat); PL=$(sym _g_pl); SEC=$(sym _g_sec); BS=$(sym _g_boss_state); BHP=$(sym _g_boss_hp)
INV=$(sym _g_dbg_invul); AIM=$(sym _g_boss_aim); SPD=$(sym _g_scroll_speed)
RFRONT=$(sym _ras_front); RN=$(sym _ras_front_n)
T="$OUT/game_m5.tcl"
cat > "$T" <<TCL
set throttle off
set f [open "$OUT/game_m5.log" w]
set fails 0
proc rd16 {a} { binary scan [debug read_block memory \$a 2] su v; return \$v }
proc rds16 {a} { binary scan [debug read_block memory \$a 2] s v; return \$v }
proc rd {a} { return [debug read memory \$a] }
proc check {name cond detail} {
  global f fails
  if {\$cond} { puts \$f "CHECK \$name OK \$detail" } else { puts \$f "CHECK \$name NG \$detail"; incr fails }
  flush \$f
}
proc key {row bit dur} { keymatrixdown \$row \$bit; after time \$dur [list keymatrixup \$row \$bit] }
set ::seq {}; set ::last -1; set ::blank 0; set ::stall 0; set ::fr_prev 0; set ::late_prev 0; set ::late_at {}
foreach s {11 13 20 22 29 31 38 41 48 50} { set ::minhp(\$s) 255 }
set ::press 0; set ::g7 0; set ::mark 0; set ::yjk_bad -1
# YJK の絵の照合(GNOMON の間に一度。6 面を出るとリングの 0..47 行は空に描き戻される)
proc yjk_check {} {
  set bad 0
  foreach {pg fn} {0 yjk_even.bin 1 yjk_odd.bin} {
    set h [open "build/\$fn" rb]; set ref [read \$h]; close \$h
    binary scan [debug read_block VRAM [expr {\$pg * 0x10000}] 6144] cu* got
    binary scan \$ref cu* want2
    foreach a \$got b \$want2 { if {\$a != \$b} { incr bad } }
  }
  set ::yjk_bad \$bad
}
# 表(ISR が読んでいる方)の項目: 行 1..187 の横スクロールの数、HR ポーリングの有無、(行, x) の組
proc raslist {} {
  set p [rd16 $RFRONT]; set n [rd $RN]; set ns 0; set hz 0; set xs {}
  for {set i 0} {\$i < \$n} {incr i} {
    binary scan [debug read_block memory [expr {\$p + \$i * 10}] 4] cucucucu line fl r26 r27
    if {\$line > 0 && \$line < 188 && (\$fl & 1)} { incr ns; lappend xs \$line [expr {(\$r26 * 8 - \$r27) & 511}] }
    if {\$fl & 4} { set hz 1 }
    if {(\$fl & 16) && \$line > 0} {
      binary scan [debug read_block memory [expr {\$p + \$i * 10 + 7}] 3] cusu hn hp
      binary scan [debug read_block memory \$hp 2] cucu r0 v0
      if {\$r0 == 23 && \$hn == 1 && \$line < 188} { incr ::press_now }
      if {\$line == 244 && \$r0 == 0 && \$v0 == 0x1E} { set ::g7 1 }
      if {\$line == 48 && (\$fl & 0x20)} { set ::mark 1 }
    }
  }
  return [list \$ns \$hz \$xs]
}
proc watch {} {
  set ph [rd [expr {$ST+14}]]
  set fr [rd16 [expr {$ST+6}]]
  if {[expr {[debug read {VDP regs} 1] & 0x40}] == 0} { incr ::blank }
  if {\$fr == \$::fr_prev} { incr ::stall }
  set lt [rd16 [expr {$ST+4}]]
  if {\$lt != \$::late_prev} { lappend ::late_at "[format %.2f [machine_info time]](+[expr {\$lt - \$::late_prev}] 区間 [rd $SEC])"; set ::late_prev \$lt }
  set ::fr_prev \$fr
  if {\$ph == 3} {
    set s [rd $SEC]
    if {\$s != \$::last} { lappend ::seq \$s; set ::last \$s; set ::t_sec(\$s) [machine_info time] }
    if {[info exists ::minhp(\$s)]} {
      set b [rd $BS]; if {(\$b == 2 || \$b == 3) && [rd $BHP] < \$::minhp(\$s)} { set ::minhp(\$s) [rd $BHP] }
    }
    if {[info exists ::spmax(\$s)]} {
      binary scan [debug read_block memory $SPD 1] c sp
      if {\$sp > \$::spmax(\$s)} { set ::spmax(\$s) \$sp }; if {\$sp < \$::spmin(\$s)} { set ::spmin(\$s) \$sp }
    }
    if {\$s == 50 && \$::yjk_bad < 0} { yjk_check }
    if {\$s == 40 || \$s >= 46} {
      set ::press_now 0
      raslist
      if {\$s == 40 && \$::press_now >= 4} { set ::press 1 }
    }
    # 自機: ボス戦はボスの高さ / 弱点の高さへ。3 面・4 面のボスでは左端へ
    set yy [dict create 3 1280 5 1248 11 960 13 1376 20 1376 22 1376 36 640 37 640 39 2400 40 1440 46 1120 47 1120 49 1280]
    if {\$s == 22 || \$s == 29 || \$s == 31} { dict set yy \$s [expr {(20 + (int([machine_info time]*4) % 36) * 4) * 16}] }
    set aim [rds16 $AIM]
    if {\$aim >= 0 && \$aim < 200} { set ay [expr {\$aim - 8}]; if {\$ay < 0} { set ay 0 }; dict set yy \$s [expr {\$ay * 16}] }
    if {[lsearch {20 22 29 31 38 41 48 50} \$s] >= 0} { debug write_block memory $PL [binary format s 384] }
    if {[dict exists \$yy \$s]} { debug write_block memory [expr {$PL+2}] [binary format s [dict get \$yy \$s]] }
  }
  after time 0.25 watch
}
after time 12.0 watch
after time 15.0 { key 8 0x01 0.1 }
after time 19.0 { debug write memory $INV 1; keymatrixdown 8 0x01 }
for {set t 20} {\$t < 1000} {incr t 4} { after time \$t [list key 8 0x20 0.6]; after time [expr {\$t+2}] [list key 8 0x40 0.6] }
# throttle off の画面は古いことがあるので、撮る前に少しだけ実時間で回す
proc shot {name} { set ::throttle on; after time 0.2 [list apply {{n} { screenshot -raw "$OUT/m5_\$n.png"; set ::throttle off }} \$name] }
proc finish {} {
  global f
  set late [rd16 [expr {$ST+4}]]
  set i [lsearch \$::seq 7]
  set want {}; for {set k 7} {\$k <= 51} {incr k} { lappend want \$k }
  check sections [expr {\$i >= 0 && [lrange \$::seq \$i [expr {\$i + 44}]] eq \$want}] "順=\$::seq"
  foreach {s name} {38 hangar_deck 41 gauge_block 48 hourglass 50 gnomon} {
    check \$name [expr {\$::minhp(\$s) == 0}] "最小の体力=\$::minhp(\$s)"
  }
  check press [expr {\$::press}] "機関部のプレス(R#23 の項目 4 つ)"
  check split [expr {\$::g7 && \$::mark}] "244 行で SCREEN12=\$::g7 48 行の境界=\$::mark"
  set bad \$::yjk_bad
  check yjk [expr {\$bad == 0}] "YJK の絵と違うバイト=\$bad / 12288"
  check no_blackout [expr {\$::blank == 0 && \$::stall == 0}] "表示が切れた回数=\$::blank 止まった回数=\$::stall"
  check no_late [expr {\$late == 0}] "遅れたフレーム=\$late \$::late_at"
  set ts ""; foreach s \$::seq { append ts " \$s@[format %.1f \$::t_sec(\$s)]" }
  puts \$f "区間の時刻:\$ts"
  if {\$::fails} { puts \$f "判定: NG (\$::fails)" } else { puts \$f "判定: OK" }
  close \$f; exit
}
proc wait_end {} {
  foreach {s name dt} {35 approach 14 36 bow 6 37 deck 10 38 hangar 6 39 under 8 40 engine 8 41 gauge 3 42 wreck 4 46 wall 6 47 shadow 10 48 hourglass 6 49 dial 8 50 gnomon 2} {
    if {[info exists ::t_sec(\$s)] && ![info exists ::shot(\$s)] && [machine_info time] > \$::t_sec(\$s) + \$dt} { set ::shot(\$s) 1; shot \$name }
  }
  if {[info exists ::t_sec(51)] && [machine_info time] > \$::t_sec(51) + 6} { finish } elseif {[machine_info time] > 950} { finish } else { after time 0.5 wait_end }
}
after time 30 wait_end
TCL
timeout 2400 openmsx -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 -script "$T" >/dev/null 2>&1 || true
cat "$OUT/game_m5.log"
grep -q "判定: OK" "$OUT/game_m5.log"
