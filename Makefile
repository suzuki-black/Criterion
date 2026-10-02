# Makefile — Criterion (MSX turboR 横スクロールシューティング)
#   make m0         M0 技術検証 ROM をすべて作る(build/m0_*.rom)
#   make clean
#   make DEFS=-DNO_RAMX   RAM 実行を止めた比較用(make clean してから)
include config.mk

BUILD := build
INC   := -Isrc/include -Isrc/m0 -I$(BUILD)
HDRS  := $(wildcard src/include/*.h)
CORE  := $(BUILD)/vdp.rel $(BUILD)/sys.rel $(BUILD)/frame.rel $(BUILD)/raster.rel $(BUILD)/font.rel $(BUILD)/pcmb.rel

M0_TESTS := scroll raster probe pcm mode
M0_ROMS  := $(M0_TESTS:%=$(BUILD)/m0_%.rom)
M0_COMMON := $(BUILD)/m0_world.rel   # M0 の検証 ROM が共有する世界(src/m0/world.c)

.PHONY: all m0 clean
.SECONDARY:
# ★レシピが失敗したらターゲットを消す。リンクが失敗(未定義シンボル等)しても .ihx が残ると、
#   次の make が「新しい」と判断してそのまま ROM を作る(前作 9ec5eae と同じ罠。フォールト注入で再現済み)。
.DELETE_ON_ERROR:
all: m0
m0: $(M0_ROMS)

$(BUILD):
	mkdir -p $@

$(BUILD)/crt0rom.rel: src/crt0rom.s | $(BUILD)
	sdasz80 -o $@ $<

$(BUILD)/%.rel: src/core/%.c $(HDRS) config.mk | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) $(INC) $< -o $@

$(BUILD)/m0_%.rel: src/m0/%.c $(HDRS) src/m0/world.h config.mk | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) $(INC) $< -o $@

# 生成ヘッダ: PCM の試験音(tools/gen_pcm.py)
$(BUILD)/pcm_data.h: tools/gen_pcm.py | $(BUILD)
	python3 tools/gen_pcm.py $@
$(BUILD)/m0_pcm.rel $(BUILD)/m0_mode.rel: $(BUILD)/pcm_data.h

# 常駐リンク。sdld は未定義シンボルを警告で済ませて call 0x0000 を出す(実機リセット)ので、
# マップに Undefined が出たら失敗させる(前作の教訓)。
$(BUILD)/m0_%.ihx: $(BUILD)/crt0rom.rel $(CORE) $(M0_COMMON) $(BUILD)/m0_%.rel
	sdcc -m$(strip $(TARGET)) --no-std-crt0 --code-loc $(strip $(CODELOC)) --data-loc $(strip $(DATALOC)) \
	    $(BUILD)/crt0rom.rel $(BUILD)/m0_$*.rel $(CORE) $(M0_COMMON) -o $@
	@if grep -q "Undefined Global" $(@:.ihx=.map); then \
	    grep -A3 "Undefined Global" $(@:.ihx=.map); echo "ERROR: 未定義シンボル"; rm -f $@; exit 1; fi

$(BUILD)/m0_%.rom: $(BUILD)/m0_%.ihx tools/rompack.mjs
	node tools/rompack.mjs --size $(strip $(M0_ROM_SIZE)) --code $< --out $@

clean:
	rm -rf $(BUILD)
