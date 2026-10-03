#!/bin/sh
# M4 の総合検証(GAME.ROM): 起動から 1 面・2 面・3 面・4 面を通し、4 面の終わりまで。
#   無敵・連射・上下移動で流す。ボス戦の間は自機をボスの高さ(g_boss_aim があればその弱点の高さ)へ置く。
#   1. 区間が順に進む: 2 面(7..14)→ 3 面(15..23)→ 4 面(24..32)
#   2. ボスを倒して進む(体力が 0 になった): COUNTERWEIGHT・KILOGRAM・METRONOME・TACHOMETER・VERNIER・STRATUM。
#      RULER(壊せない)は 4 回通り抜けて、時間切れ(90 秒)より前に終わる
#   3. 3 面の速さ: 加速で 4 ドット/フレーム(16)以上、最高速で 8(32)、停止で 0、逆走で -3 ドット/フレーム(-12)以下
#   4. 4 面の帯: 大気上層で 6 帯、雲海で 12 帯(横位置が逆向きに動く帯がある)、陽炎帯で HR ポーリングの項目、
#      STRATUM で顔の 6 帯 + 上下
#   5. 暗転なし・遅れたフレーム 0(起動から 4 面の終わりまで)
# throttle off で流す(時刻はエミュレータの時刻)。画面を数枚撮る(m4_*.png)。
#   usage: tools/test/run_game_m4.sh <作業ディレクトリ>
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
PCMOFF=$(awk '$2=="_g_pcm_off"{print $3}' "$NOI")   # ★PCM は切る(エンジンの時間を見る。PCM は run_game_m7.sh)
T="$OUT/game_m4.tcl"
cat > "$T" <<TCL
set throttle off
after time 6 { debug write memory $PCMOFF 1 }
set f [open "$OUT/game_m4.log" w]
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
foreach s {11 13 20 22 29 31} { set ::minhp(\$s) 255 }
foreach s {17 18 19 21} { set ::spmax(\$s) -128; set ::spmin(\$s) 127 }
foreach s {26 27 28} { set ::bands(\$s) 0 }
set ::haze 0; set ::rev 0; set ::fwd 0; set ::prevx {}
# 表(ISR が読んでいる方)の項目: 行 1..187 の横スクロールの数、HR ポーリングの有無、(行, x) の組
proc raslist {} {
  set p [rd16 $RFRONT]; set n [rd $RN]; set ns 0; set hz 0; set xs {}
  for {set i 0} {\$i < \$n} {incr i} {
    binary scan [debug read_block memory [expr {\$p + \$i * 10}] 4] cucucucu line fl r26 r27
    if {\$line > 0 && \$line < 188 && (\$fl & 1)} { incr ns; lappend xs \$line [expr {(\$r26 * 8 - \$r27) & 511}] }
    if {\$fl & 4} { set hz 1 }
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
    if {\$s >= 24} {
      lassign [raslist] ns hz xs
      if {[info exists ::bands(\$s)] && \$ns > \$::bands(\$s)} { set ::bands(\$s) \$ns }
      if {\$s == 28 && \$hz} { set ::haze 1 }
      if {\$s == 31 && [rd $BS] == 2} { foreach {l x} \$xs { if {[lsearch {22 46 70 94 118 142 166} \$l] >= 0} { set ::face(\$l) 1 } } }
      if {\$s == 27} {
        # 同じ行の横位置の動き(0.25 秒): 前へ流れる帯と逆へ流れる帯の両方がある
        foreach {l x} \$xs { if {[dict exists \$::prevx \$l]} { set d [expr {(\$x - [dict get \$::prevx \$l]) & 511}]; if {\$d > 0 && \$d < 64} { set ::fwd 1 } elseif {\$d > 448} { set ::rev 1 } } }
        set ::prevx [dict create {*}\$xs]
      }
    }
    # 自機: ボス戦はボスの高さ / 弱点の高さへ。3 面・4 面のボスでは左端へ
    set yy [dict create 3 1280 5 1248 11 960 13 1376 20 1376 22 1376]
    if {\$s == 22 || \$s == 29 || \$s == 31} { dict set yy \$s [expr {(20 + (int([machine_info time]*4) % 36) * 4) * 16}] }
    set aim [rds16 $AIM]
    if {\$aim >= 0 && \$aim < 200} { set ay [expr {\$aim - 8}]; if {\$ay < 0} { set ay 0 }; dict set yy \$s [expr {\$ay * 16}] }
    if {\$s == 20 || \$s == 22 || \$s == 29 || \$s == 31} { debug write_block memory $PL [binary format s 384] }
    if {[dict exists \$yy \$s]} { debug write_block memory [expr {$PL+2}] [binary format s [dict get \$yy \$s]] }
  }
  after time 0.25 watch
}
after time 12.0 watch
after time 15.0 { key 8 0x01 0.1 }
after time 19.0 { debug write memory $INV 1; keymatrixdown 8 0x01 }
for {set t 20} {\$t < 700} {incr t 4} { after time \$t [list key 8 0x20 0.6]; after time [expr {\$t+2}] [list key 8 0x40 0.6] }
# throttle off の画面は古いことがあるので、撮る前に少しだけ実時間で回す
proc shot {name} { set ::throttle on; after time 0.2 [list apply {{n} { screenshot -raw "$OUT/m4_\$n.png"; set ::throttle off }} \$name] }
proc finish {} {
  global f
  set late [rd16 [expr {$ST+4}]]
  set i [lsearch \$::seq 7]
  set want {}; for {set k 7} {\$k <= 32} {incr k} { lappend want \$k }
  check sections [expr {\$i >= 0 && [lrange \$::seq \$i end] eq \$want}] "順=\$::seq"
  foreach {s name} {11 counterweight 13 kilogram 20 metronome 22 tachometer 29 vernier 31 stratum} {
    check \$name [expr {\$::minhp(\$s) == 0}] "最小の体力=\$::minhp(\$s)"
  }
  set rt [expr {[info exists ::t_sec(31)] && [info exists ::t_sec(30)] ? \$::t_sec(31) - \$::t_sec(30) : 999}]
  check ruler [expr {\$rt < 85}] "通り抜けた秒数=[format %.1f \$rt]"
  check speed_top [expr {\$::spmax(17) >= 16 && \$::spmax(21) == 32}] "加速=\$::spmax(17) 最高速=\$::spmax(21)"
  check speed_stop [expr {\$::spmin(18) == 0}] "停止の最小=\$::spmin(18)"
  check speed_rev [expr {\$::spmin(19) <= -12}] "逆走の最小=\$::spmin(19)"
  check bands_upper [expr {\$::bands(26) >= 5}] "大気上層の帯(0 行より下)=\$::bands(26)"
  check bands_sea [expr {\$::bands(27) >= 11 && \$::fwd && \$::rev}] "雲海の帯=\$::bands(27) 前へ=\$::fwd 逆へ=\$::rev"
  check haze [expr {\$::haze}] "陽炎帯の HR ポーリング"
  check bands_stratum [expr {[array size ::face] == 7}] "STRATUM の顔の帯の行=[lsort -integer [array names ::face]]"
  check no_blackout [expr {\$::blank == 0 && \$::stall == 0}] "表示が切れた回数=\$::blank 止まった回数=\$::stall"
  check no_late [expr {\$late <= 2}] "遅れたフレーム=\${late}(★BGM のドライバが最も重い場面の山に重なると 1 フレーム遅れることがある。1 周で 2 まで許す。docs/TODO.md) \$::late_at"
  set ts ""; foreach s \$::seq { append ts " \$s@[format %.1f \$::t_sec(\$s)]" }
  puts \$f "区間の時刻:\$ts"
  if {\$::fails} { puts \$f "判定: NG (\$::fails)" } else { puts \$f "判定: OK" }
  close \$f; exit
}
# 4 面の終わり(区間 32)に入って 6 秒で終える(入らなければ 640 秒で打ち切り)。区間に入って少しで画面を撮る
proc wait_end {} {
  foreach {s name dt} {17 accel 12 19 reverse 3 20 metronome 4 22 tachometer 4 26 upper 4 27 sea 6 28 haze 4 29 vernier 6 30 ruler 12 31 stratum 8} {
    if {[info exists ::t_sec(\$s)] && ![info exists ::shot(\$s)] && [machine_info time] > \$::t_sec(\$s) + \$dt} { set ::shot(\$s) 1; shot \$name }
  }
  if {[info exists ::t_sec(32)] && [machine_info time] > \$::t_sec(32) + 6} { finish } elseif {[machine_info time] > 640} { finish } else { after time 0.5 wait_end }
}
after time 30 wait_end
TCL
timeout 1500 openmsx -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 -script "$T" >/dev/null 2>&1 || true
cat "$OUT/game_m4.log"
grep -q "判定: OK" "$OUT/game_m4.log"
