# Makefile — Criterion (MSX turboR 横スクロールシューティング)
#   make m0         M0 技術検証 ROM をすべて作る(build/m0_*.rom)
#   make clean
#   make DEFS=-DNO_RAMX   RAM 実行を止めた比較用(make clean してから)
include config.mk

BUILD := build
comma := ,
INC   := -Isrc/include -Isrc/m0 -I$(BUILD)
HDRS  := $(wildcard src/include/*.h)
CORE  := $(BUILD)/vdp.rel $(BUILD)/sys.rel $(BUILD)/frame.rel $(BUILD)/raster.rel $(BUILD)/font.rel $(BUILD)/pcmb.rel $(BUILD)/pcmc.rel $(BUILD)/input.rel $(BUILD)/spr.rel

M0_TESTS := scroll raster probe pcm mode
M0_ROMS  := $(M0_TESTS:%=$(BUILD)/m0_%.rom)
M0_COMMON := $(BUILD)/m0_world.rel   # M0 の検証 ROM が共有する世界(src/m0/world.c)

GAME_SRCS := $(wildcard src/game/*.c)
GAME_RELS := $(GAME_SRCS:src/game/%.c=$(BUILD)/g_%.rel)
GEN_HDRS  := $(BUILD)/pcm_data.h $(BUILD)/sprite_data.h $(BUILD)/hud_data.h $(BUILD)/assets.h $(BUILD)/city_data.h $(BUILD)/s2_data.h $(BUILD)/s3_data.h $(BUILD)/s4_data.h $(BUILD)/s5_data.h $(BUILD)/s6_data.h $(BUILD)/s8_data.h $(BUILD)/snd_data.h

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
$(BUILD)/stage2.stamp: tools/gen_stage2.py | $(BUILD)
	python3 tools/gen_stage2.py $(BUILD) $(BUILD)/s2_data.h
	touch $@
$(BUILD)/s2_data.h: $(BUILD)/stage2.stamp
$(BUILD)/stage3.stamp: tools/gen_stage3.py | $(BUILD)
	python3 tools/gen_stage3.py $(BUILD) $(BUILD)/s3_data.h
	touch $@
$(BUILD)/s3_data.h: $(BUILD)/stage3.stamp
$(BUILD)/stage4.stamp: tools/gen_stage4.py | $(BUILD)
	python3 tools/gen_stage4.py $(BUILD) $(BUILD)/s4_data.h
	touch $@
$(BUILD)/s4_data.h: $(BUILD)/stage4.stamp
$(BUILD)/stage5.stamp: tools/gen_stage5.py | $(BUILD)
	python3 tools/gen_stage5.py $(BUILD) $(BUILD)/s5_data.h
	touch $@
$(BUILD)/s5_data.h: $(BUILD)/stage5.stamp
$(BUILD)/stage6.stamp: tools/gen_stage6.py | $(BUILD)
	python3 tools/gen_stage6.py $(BUILD) $(BUILD)/s6_data.h
	touch $@
$(BUILD)/s6_data.h: $(BUILD)/stage6.stamp
$(BUILD)/stage8.stamp: tools/gen_stage8.py | $(BUILD)
	python3 tools/gen_stage8.py $(BUILD) $(BUILD)/s8_data.h
	touch $@
$(BUILD)/s8_data.h: $(BUILD)/stage8.stamp
# 音(tools/gen_sound.py: 曲の MML と PCM の合成 → snd.bin、ROM のバンク 40 から)
$(BUILD)/snd.stamp: tools/gen_sound.py tools/songs.py tools/snd_pcm.py $(wildcard assets/pcm/*.wav) | $(BUILD)
	python3 tools/gen_sound.py $(BUILD)/snd.bin $(BUILD)/snd_data.h
	touch $@
$(BUILD)/snd_data.h $(BUILD)/snd.bin: $(BUILD)/snd.stamp
$(BUILD)/assets.stamp: tools/pack_assets.py $(BUILD)/title.stamp $(BUILD)/city.stamp $(BUILD)/boss.stamp $(BUILD)/stage2.stamp $(BUILD)/stage3.stamp $(BUILD)/stage4.stamp $(BUILD)/stage5.stamp $(BUILD)/stage6.stamp $(BUILD)/stage8.stamp
	python3 tools/pack_assets.py $(BUILD)/assets.bin $(BUILD)/assets.h \
	    TITLE_EVEN=$(BUILD)/title_even.bin TITLE_ODD=$(BUILD)/title_odd.bin LOGO=$(BUILD)/logo.bin \
	    TDF=$(BUILD)/tdf.bin CITY_STRIPS=$(BUILD)/city_strips.bin CITY_MAP=$(BUILD)/city_map.bin \
	    BOB=$(BUILD)/bob.bin S2_STRIPS=$(BUILD)/s2_strips.bin S2_MAP=$(BUILD)/s2_map.bin KG=$(BUILD)/kg.bin \
	    S3_STRIPS=$(BUILD)/s3_strips.bin S3_MAP=$(BUILD)/s3_map.bin TACHO=$(BUILD)/tacho.bin \
	    S4_STRIPS=$(BUILD)/s4_strips.bin S4_MAP=$(BUILD)/s4_map.bin FACE=$(BUILD)/face.bin S4_PARTS=$(BUILD)/s4_parts.bin \
	    S5_STRIPS=$(BUILD)/s5_strips.bin S5_MAP=$(BUILD)/s5_map.bin TURRET=$(BUILD)/turret.bin FAR=$(BUILD)/far.bin \
	    S5_SPR=$(BUILD)/s5_spr.bin GAUGE=$(BUILD)/gauge.bin \
	    S6_STRIPS=$(BUILD)/s6_strips.bin S6_MAP=$(BUILD)/s6_map.bin YJK_EVEN=$(BUILD)/yjk_even.bin YJK_ODD=$(BUILD)/yjk_odd.bin \
	    GEAR=$(BUILD)/gear.bin HOURGLASS=$(BUILD)/hourglass.bin S6_SPR=$(BUILD)/s6_spr.bin \
	    S8_STRIPS=$(BUILD)/s8_strips.bin S8_MAP=$(BUILD)/s8_map.bin TBAR=$(BUILD)/tbar.bin BELL_O=$(BUILD)/bell_o.bin BELL_I=$(BUILD)/bell_i.bin
	touch $@
$(BUILD)/assets.bin $(BUILD)/assets.h: $(BUILD)/assets.stamp

# ---- 本体(GAME.ROM, 1MB) ----
# ★割込み処理(raster)と ROM 読み出し(rom)はページ 1(0x8000 未満)に置く。ROM を読む間はページ 2 が
#   カートリッジの別のバンクになるため(rom.c)。リンクの先頭に並べ、リンク後に番地を検査する。
#   音のドライバ(snd)も曲を読む間はページ 2 が曲のバンクになるので、同じくページ 1 に置く(snd.c)。
GAME_FIRST := $(BUILD)/raster.rel $(BUILD)/rom.rel $(BUILD)/g_snd.rel
GAME_REST  := $(filter-out $(GAME_FIRST) $(BUILD)/pcmb.rel $(BUILD)/pcmc.rel,$(CORE))   # pcmb(方式 B の試作)・pcmc(方式 A/C の制御)は M0 の検証用
GAME_MID   := $(filter-out $(GAME_FIRST),$(GAME_RELS))
# ★ページ 0 のコード(CODE0、0x0100-OVL_BASE): 共通のエンジン(ROTOR / GHOST)。起動時に ROM の bank4-5 から
#   RAM へ複製する(main.c の code0_load。直接割込み方式の間はページ 0 が RAM)。複製の前には呼ばないこと。
CODE0_CORE := rotor ghost
GAME_CODE0_CORE := $(CODE0_CORE:%=$(BUILD)/%.rel)
# 選曲と効果音(music.c)は CODE0 へ(常駐に空きが無い)
$(BUILD)/g_music.rel: src/game/music.c $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) --opt-code-size $(DEFS) --codeseg CODE0 --constseg CODE0 -Isrc/include -Isrc/game -I$(BUILD) $< -o $@
$(BUILD)/g_%.rel: src/game/%.c $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

# ---- オーバレイ(src/include/ovl.h): ページ 0 の OVL_BASE..OVL_END へ ROM から読み込む場面のコード ----
#   本体を先にリンクし、本体の .noi から大域シンボルを絶対番地で定義した resident.rel を作って、オーバレイを
#   別にリンクする。本体からは JP 表の番地(ovl_entries.rel)で呼ぶ。
OVL_BASE := 0x1000
OVL_END  := 0x3800
OVL_DBEG := 0xD200
OVL_DEND := 0xD800
OVL_TITLE_EXPORTS := title_load title_start title_update title_draw title_vram title_build
OVL_STAGE_EXPORTS := ovl_init ovl_update ovl_logic ovl_draw ovl_vram ovl_bands
OVLS := title s1 s2 s3 s4 s5 s6 s7 s8 s8b ed
OVL_title_RELS := $(patsubst src/ovl/title/%.c,$(BUILD)/ov_title_%.rel,$(wildcard src/ovl/title/*.c))
OVL_s1_RELS    := $(patsubst src/ovl/s1/%.c,$(BUILD)/ov_s1_%.rel,$(wildcard src/ovl/s1/*.c))
OVL_s2_RELS    := $(patsubst src/ovl/s2/%.c,$(BUILD)/ov_s2_%.rel,$(wildcard src/ovl/s2/*.c))
OVL_title_HDR  := title
OVL_s2_HDR     := stage
OVL_s3_RELS    := $(patsubst src/ovl/s3/%.c,$(BUILD)/ov_s3_%.rel,$(wildcard src/ovl/s3/*.c))
OVL_s3_HDR     := stage
OVL_s4_RELS    := $(patsubst src/ovl/s4/%.c,$(BUILD)/ov_s4_%.rel,$(wildcard src/ovl/s4/*.c))
OVL_s4_HDR     := stage
OVL_s5_RELS    := $(patsubst src/ovl/s5/%.c,$(BUILD)/ov_s5_%.rel,$(wildcard src/ovl/s5/*.c))
OVL_s5_HDR     := stage
OVL_s6_RELS    := $(patsubst src/ovl/s6/%.c,$(BUILD)/ov_s6_%.rel,$(wildcard src/ovl/s6/*.c))
OVL_s6_HDR     := stage
OVL_s7_RELS    := $(patsubst src/ovl/s7/%.c,$(BUILD)/ov_s7_%.rel,$(wildcard src/ovl/s7/*.c))
OVL_s7_HDR     := stage
OVL_s8_RELS    := $(patsubst src/ovl/s8/%.c,$(BUILD)/ov_s8_%.rel,$(wildcard src/ovl/s8/*.c))
OVL_s8_HDR     := stage
OVL_s8b_RELS   := $(patsubst src/ovl/s8b/%.c,$(BUILD)/ov_s8b_%.rel,$(wildcard src/ovl/s8b/*.c))
OVL_s8b_HDR    := stage
OVL_ed_RELS    := $(patsubst src/ovl/ed/%.c,$(BUILD)/ov_ed_%.rel,$(wildcard src/ovl/ed/*.c))
OVL_ed_HDR     := stage
OVL_s1_HDR     := stage
OVL_BINS := $(OVLS:%=$(BUILD)/ovl_%.bin)

$(BUILD)/ov_title_%.rel: src/ovl/title/%.c $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@
$(BUILD)/ov_s1_%.rel: src/ovl/s1/%.c src/ovl/s1/s1.h $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

$(BUILD)/ov_s2_%.rel: src/ovl/s2/%.c src/ovl/s2/s2.h $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

$(BUILD)/ov_s3_%.rel: src/ovl/s3/%.c src/ovl/s3/s3.h $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

$(BUILD)/ov_s4_%.rel: src/ovl/s4/%.c src/ovl/s4/s4.h $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

$(BUILD)/ov_s5_%.rel: src/ovl/s5/%.c src/ovl/s5/s5.h $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

$(BUILD)/ov_s6_%.rel: src/ovl/s6/%.c src/ovl/s6/s6.h $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

$(BUILD)/ov_s7_%.rel: src/ovl/s7/%.c src/ovl/s7/s7.h $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

$(BUILD)/ov_s8_%.rel: src/ovl/s8/%.c src/ovl/s8/s8.h $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

$(BUILD)/ov_s8b_%.rel: src/ovl/s8b/%.c src/ovl/s8/s8.h $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

$(BUILD)/ov_ed_%.rel: src/ovl/ed/%.c $(HDRS) src/game/game.h $(GEN_HDRS) config.mk Makefile | $(BUILD)
	sdcc -m$(strip $(TARGET)) -c $(OPT) $(DEFS) -Isrc/include -Isrc/game -I$(BUILD) $< -o $@

$(BUILD)/ovlhdr_title.s: tools/gen_ovl.py Makefile | $(BUILD)
	python3 tools/gen_ovl.py hdr $@ $(OVL_TITLE_EXPORTS)
$(BUILD)/ovlhdr_stage.s: tools/gen_ovl.py Makefile | $(BUILD)
	python3 tools/gen_ovl.py hdr $@ $(OVL_STAGE_EXPORTS)
$(BUILD)/ovl_entries.s: tools/gen_ovl.py Makefile | $(BUILD)
	python3 tools/gen_ovl.py entries $@ $(OVL_BASE) $(join $(addsuffix $(comma),$(OVL_TITLE_EXPORTS)),$(OVL_STAGE_EXPORTS))
$(BUILD)/%.rel: $(BUILD)/%.s
	sdasz80 -o $@ $<
$(BUILD)/resident.s: $(BUILD)/game.ihx tools/gen_ovl.py
	python3 tools/gen_ovl.py resident $(BUILD)/game.noi $@ $(OVL_TITLE_EXPORTS) $(OVL_STAGE_EXPORTS)

.SECONDEXPANSION:
$(BUILD)/ovl_%.ihx: $(BUILD)/ovlhdr_$$(OVL_$$*_HDR).rel $$(OVL_$$*_RELS) $(BUILD)/resident.rel
	sdcc -m$(strip $(TARGET)) --no-std-crt0 --code-loc $(OVL_BASE) --data-loc $(OVL_DBEG) \
	    $(BUILD)/ovlhdr_$(OVL_$*_HDR).rel $(OVL_$*_RELS) $(BUILD)/resident.rel -o $@
	@if grep -q "Undefined Global" $(@:.ihx=.map); then \
	    grep -A3 "Undefined Global" $(@:.ihx=.map); echo "ERROR: 未定義シンボル(オーバレイ $*)"; rm -f $@; exit 1; fi
$(BUILD)/ovl_%.bin: $(BUILD)/ovl_%.ihx tools/gen_ovl.py
	python3 tools/gen_ovl.py bin $< $(<:.ihx=.map) $@ $(OVL_BASE) $(OVL_END) $(OVL_DBEG) $(OVL_DEND)

$(BUILD)/game.ihx: $(BUILD)/crt0rom.rel $(CORE) $(BUILD)/rom.rel $(GAME_RELS) $(GAME_CODE0_CORE) $(BUILD)/ovl_entries.rel
	sdcc -m$(strip $(TARGET)) --no-std-crt0 --code-loc $(strip $(CODELOC)) --data-loc $(strip $(DATALOC)) \
	    -Wl-b_CODE0=0x0100 $(BUILD)/crt0rom.rel $(GAME_FIRST) $(GAME_MID) $(GAME_REST) $(GAME_CODE0_CORE) \
	    $(BUILD)/ovl_entries.rel -o $@
	@if grep -q "Undefined Global" $(@:.ihx=.map); then \
	    grep -A3 "Undefined Global" $(@:.ihx=.map); echo "ERROR: 未定義シンボル"; rm -f $@; exit 1; fi
	@E=$$(awk '$$2=="s__CODE0"{s=$$3} $$2=="l__CODE0"{l=$$3} END{printf "%d", s+l}' $(@:.ihx=.noi)); \
	    if [ $$E -gt $$(($(OVL_BASE))) ]; then echo "ERROR: CODE0 の末尾 $$E がオーバレイの窓 $(OVL_BASE) に入った(ovl.h)"; rm -f $@; exit 1; fi
	@E=$$(awk '$$2=="s__DATA"{s=$$3} $$2=="l__DATA"{l=$$3} END{printf "%d", s+l}' $(@:.ihx=.noi)); \
	    if [ $$E -gt $$(($(OVL_DBEG))) ]; then echo "ERROR: _DATA の末尾 $$E がオーバレイの変数 $(OVL_DBEG) に入った(ovl.h)"; rm -f $@; exit 1; fi
	@for S in _ras_isr_direct _ras_isr_bios _ras_apply_next _ras_vblank _rom_copy _snd_frame _snd_end; do \
	    A=$$(awk -v n=$$S '$$2==n{print $$3}' $(@:.ihx=.noi)); \
	    if [ -z "$$A" ] || [ $$(printf '%d' $$A) -ge 32768 ]; then \
	        echo "ERROR: $$S=$$A がページ 1(0x8000 未満)に無い。ROM を読む間に壊れる(rom.c)"; rm -f $@; exit 1; fi; \
	done

GAME.ROM: $(BUILD)/game.ihx $(BUILD)/assets.bin $(BUILD)/snd.bin $(OVL_BINS) tools/rompack.mjs
	node tools/rompack.mjs --code $< --out $@ --asset 6 $(BUILD)/assets.bin --asset 40 $(BUILD)/snd.bin \
	    --asset 104 $(BUILD)/ovl_title.bin --asset 106 $(BUILD)/ovl_s1.bin --asset 108 $(BUILD)/ovl_s2.bin --asset 110 $(BUILD)/ovl_s3.bin --asset 112 $(BUILD)/ovl_s4.bin --asset 114 $(BUILD)/ovl_s5.bin --asset 116 $(BUILD)/ovl_s6.bin --asset 118 $(BUILD)/ovl_s7.bin --asset 120 $(BUILD)/ovl_s8.bin --asset 122 $(BUILD)/ovl_s8b.bin --asset 124 $(BUILD)/ovl_ed.bin

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
