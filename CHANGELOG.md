# Changelog

形式は [Keep a Changelog](https://keepachangelog.com/ja/1.1.0/)、バージョンは [SemVer](https://semver.org/lang/ja/) に従う。

## [Unreleased]

### Changed
- VDP コマンドの発行を 5 本ずつ 3 回に分けた（割込み禁止を 1 ライン未満に）。
- 直接割込みでは R#15 を常に 1 にし、FH の判定を IN 1 回にした。
- PCM の刻み 244 では、ラスター項目の適用を先、穴埋めを最後にした。

### Added
- プロジェクトの骨格（Makefile / config.mk / crt0 / VDP・システム最小層 / rompack）。
- M0-1 技術検証 ROM `build/m0_scroll.rom`: SP2 リングでの 0.5〜8 ドット/フレーム可変速・急停止・逆走。
- フレーム同期 `frame_sync`（絶対時刻。前作 ZeroNoHoukouR 111591e の教訓）。
- `.DELETE_ON_ERROR:` と未定義シンボル検査（失敗した `.ihx` が残って ROM が焼かれる罠の防止）。
- ラスターリスト `src/core/raster.c`（ダブルバッファ、HR ポーリング帯、直接割込み／BIOS 割込み）。
- RAM 実行 `sys_ram_exec`、自前フォント `src/core/font.c`。
- M0-2 技術検証 ROM `build/m0_raster.rom` と自動検証 `tools/test/run_m0_raster.sh`。
- PCM 方式 A/C（`raster.c` の刻み方式）と方式 B（`pcmb.c`）、試験音の合成 `tools/gen_pcm.py`。
- M0-3 技術検証 ROM `build/m0_pcm.rom`・探り ROM `build/m0_probe.rom` と計測 `tools/test/run_m0_pcm.sh`。
- ラスターリストの項目に `RAS_REGS`（レジスタ列）と `RAS_MARK`（適用の合図）。
- M0-4 技術検証 ROM `build/m0_mode.rom`（SCREEN 12/5 の画面分割）と自動検証 `tools/test/run_m0_mode.sh`。
- M0-1 の自動検証 `tools/test/run_m0_scroll.sh`（openMSX ヘッドレス＋画素照合）。
