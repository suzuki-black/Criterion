# config.mk — ビルド設定の単一の正(Makefile から include)。
TARGET   = z80          # SDCC の -mr800 は実験的なので z80 で生成(前作と同じ)
CODELOC  = 0x4010       # "AB" ヘッダ直後
DATALOC  = 0xC000
# ★レジスタ割付の探索を深くする(9000)。前作 ZeroNoHoukouR 2d99954 で常駐が 800B 以上縮み、
#   スピルが減って速くもなった(トレードオフ無し)。ただしファイルによっては太る/sdcc が内部エラーに
#   なる例があった(前作の player/ramexec/entity)。その場合はそのファイルだけ 3000 に戻す。
OPT      = --max-allocs-per-node 9000
M0_ROM_SIZE = 0x10000   # M0 テスト ROM(ASCII8, 64KB)
