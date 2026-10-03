# Makefile — Criterion (MSX turboR 横スクロールシューティング)
#   make m0         M0 技術検証 ROM をすべて作る(build/m0_*.rom)
#   make clean
#   make DEFS=-DNO_RAMX   RAM 実行を止めた比較用(make clean してから)
include config.mk

BUILD := build
INC   := -Isrc/include -Isrc/m0 -I$(BUILD)
HDRS  := $(wildcard src/include/*.h)
CORE  := $(BUILD)/vdp.rel $(BUILD)/sys.rel $(BUILD)/frame.rel $(BUILD)/raster.rel $(BUILD)/font.rel $(BUILD)/pcmb.rel $(BUILD)/input.rel $(BUILD)/spr.rel

M0_TESTS := scroll raster probe pcm mode
M0_ROMS  := $(M0_TESTS:%=$(BUILD)/m0_%.rom)
M0_COMMON := $(BUILD)/m0_world.rel   # M0 の検証 ROM が共有する世界(src/m0/world.c)

GAME_SRCS := $(wildcard src/game/*.c)
GAME_RELS := $(GAME_SRCS:src/game/%.c=$(BUILD)/g_%.rel)
GEN_HDRS  := $(BUILD)/pcm_data.h $(BUILD)/sprite_data.h $(BUILD)/hud_data.h $(BUILD)/assets.h $(BUILD)/city_data.h

.PHONY: all m0 clean
.SECONDARY:
# ★レシピが失敗したらターゲットを消す。リンクが失敗(未定義シンボル等)しても .ihx が残ると、
#   次の make が「新しい」と判断してそのまま ROM を作る(前作 9ec5eae と同じ罠。フォールト注入で再現済み)。
.DELETE_ON_ERROR:
all: m0 GAME.ROM
m0: $(M0_ROMS)

$(BUILD):
	mkdir -p $@

$(BUILD)/crt0rom.rel: src/crt0rom.s | $(BUILD)
	sdasz80 -o $@ $<

$(BUILD)/%.rel: src/core/%.c $(HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) $(if $(filter $(CODE0_CORE),$*),--codeseg CODE0 --constseg CODE0) $(INC) $< -o $@

$(BUILD)/m0_%.rel: src/m0/%.c $(HDRS) src/m0/world.h config.mk | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) $(INC) $< -o $@

# 生成ヘッダ: PCM の試験音(tools/gen_pcm.py)
$(BUILD)/pcm_data.h: tools/gen_pcm.py | $(BUILD)
	python3 tools/gen_pcm.py $@
$(BUILD)/m0_pcm.rel $(BUILD)/m0_mode.rel: $(BUILD)/pcm_data.h

$(BUILD)/sprite_data.h: tools/gen_sprites.py | $(BUILD)
	python3 tools/gen_sprites.py $@
$(BUILD)/hud_data.h: tools/gen_hud.py | $(BUILD)
	python3 tools/gen_hud.py $@

# ---- 素材(ROM のバンク 4 以降) ----
# ★macOS の make 3.81 は複数出力のまとめ書き(&:)が使えないので、目印ファイル(.stamp)で束ねる
TITLE_BINS := $(BUILD)/title_even.bin $(BUILD)/title_odd.bin $(BUILD)/logo.bin $(BUILD)/tdf.bin
$(BUILD)/title.stamp: tools/gen_title.py | $(BUILD)
	python3 tools/gen_title.py $(BUILD)
	touch $@
$(BUILD)/city.stamp: tools/gen_city.py | $(BUILD)
	python3 tools/gen_city.py $(BUILD) $(BUILD)/city_data.h
	touch $@
$(BUILD)/city_data.h: $(BUILD)/city.stamp
$(BUILD)/boss.stamp: tools/gen_boss.py | $(BUILD)
	python3 tools/gen_boss.py $(BUILD)
	touch $@
$(BUILD)/assets.stamp: tools/pack_assets.py $(BUILD)/title.stamp $(BUILD)/city.stamp $(BUILD)/boss.stamp
	python3 tools/pack_assets.py $(BUILD)/assets.bin $(BUILD)/assets.h \
	    TITLE_EVEN=$(BUILD)/title_even.bin TITLE_ODD=$(BUILD)/title_odd.bin LOGO=$(BUILD)/logo.bin \
	    TDF=$(BUILD)/tdf.bin CITY_STRIPS=$(BUILD)/city_strips.bin CITY_MAP=$(BUILD)/city_map.bin \
	    BOB=$(BUILD)/bob.bin
	touch $@
$(BUILD)/assets.bin $(BUILD)/assets.h: $(BUILD)/assets.stamp

# ---- 本体(GAME.ROM, 1MB) ----
# ★割込み処理(raster)と ROM 読み出し(rom)はページ 1(0x8000 未満)に置く。ROM を読む間はページ 2 が
#   カートリッジの別のバンクになるため(rom.c)。リンクの先頭に並べ、リンク後に番地を検査する。
GAME_FIRST := $(BUILD)/raster.rel $(BUILD)/rom.rel
GAME_REST  := $(filter-out $(GAME_FIRST),$(CORE))
# ★ページ 0 のコード(CODE0、0x0100-0x3FFF): 場面ごとのコード。起動時に ROM の bank4-5 から RAM へ複製する
#   (main.c の code0_load。直接割込み方式の間はページ 0 が RAM)。複製の前(起動の最初)には呼ばないこと。
CODE0_GAME := title boss pawn
CODE0_CORE := rotor ghost
GAME_CODE0_CORE := $(CODE0_CORE:%=$(BUILD)/%.rel)
CODE0_FLAGS = $(if $(filter $(CODE0_GAME),$*),--codeseg CODE0 --constseg CODE0)
$(BUILD)/g_%.rel: src/game/%.c $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) $(CODE0_FLAGS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

$(BUILD)/game.ihx: $(BUILD)/crt0rom.rel $(CORE) $(BUILD)/rom.rel $(GAME_RELS) $(GAME_CODE0_CORE)
	sdcc -m$(strip $(TARGET)) --no-std-crt0 --code-loc $(strip $(CODELOC)) --data-loc $(strip $(DATALOC)) \
	    -Wl-b_CODE0=0x0100 $(BUILD)/crt0rom.rel $(GAME_FIRST) $(GAME_RELS) $(GAME_REST) $(GAME_CODE0_CORE) -o $@
	@if grep -q "Undefined Global" $(@:.ihx=.map); then \
	    grep -A3 "Undefined Global" $(@:.ihx=.map); echo "ERROR: 未定義シンボル"; rm -f $@; exit 1; fi
	@for S in _ras_isr_direct _ras_isr_bios _ras_apply_next _ras_vblank _rom_copy; do \
	    A=$$(awk -v n=$$S '$$2==n{print $$3}' $(@:.ihx=.noi)); \
	    if [ -z "$$A" ] || [ $$(printf '%d' $$A) -ge 32768 ]; then \
	        echo "ERROR: $$S=$$A がページ 1(0x8000 未満)に無い。ROM を読む間に壊れる(rom.c)"; rm -f $@; exit 1; fi; \
	done

GAME.ROM: $(BUILD)/game.ihx $(BUILD)/assets.bin tools/rompack.mjs
	node tools/rompack.mjs --code $< --out $@ --asset 6 $(BUILD)/assets.bin

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
