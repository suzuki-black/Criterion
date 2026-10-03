# Changelog

形式は [Keep a Changelog](https://keepachangelog.com/ja/1.1.0/)、バージョンは [SemVer](https://semver.org/lang/ja/) に従う。

## [Unreleased]

### Changed
- ラスターの ISR は、6 ライン未満の次の項目を割込みを待たずに続けて適用する（近い項目でフレームの残りが消えていた）。
- VRAM の配置: スプライトの表をページ 3 へ、HUD をページ 2 へ（表示リングの 256 行をすべてプレイエリアに）。
- 雲の描画を asm と表引きに（起動と再開が約 3 秒 → 約 1 秒）。
- VDP コマンドの発行を 5 本ずつ 3 回に分けた（割込み禁止を 1 ライン未満に）。
- 直接割込みでは R#15 を常に 1 にし、FH の判定を IN 1 回にした。
- PCM の刻み 244 では、ラスター項目の適用を先、穴埋めを最後にした。

### Added
- **M3: ROTOR と GHOST**。回転拡縮 `src/core/rotor.c`（BG 用 4bpp / スプライト用 1bpp）、VDP コマンドの待ち行列
  `src/core/ghost.c`、BG に直接描く Pawn `src/game/pawn.c`、PLUMB（ROTOR で作った下げ振りを BG に）、
  LEGION（Pawn の粒子が積み上がる人型）、∞ の紋章 `src/game/infx.c`（確定の演出・タイトル）。
- ページ 0 の RAM を 2 つ目のコード領域 CODE0 に（ROM の bank4-5 から起動時に複製。素材は bank6 から）。
- 処理落ち（重いフレームは敵の更新とスプライトの描き直しを飛ばす。スクロールは 60Hz のまま）。
- 総合検証 `tools/test/run_game_m3.sh`（表示リング・LEGION・ROTOR を VRAM で照合）。
- **M2: 縦切り 1 面**。起動ロゴ → タイトル（SCREEN 12/5 分割）→ 出撃 → 空中戦 → 降下 → 都市 → 中ボス LEGION（仮）→
  都市後半 → ボス PLUMB（仮）→ 上昇 → 面の開始表示 → 2 面の空中戦まで、暗転なしで通る（`src/game/title.c` / `stage.c` / `boss.c`）。
- 縦に長い世界を表示リングで回す仕組み（8 行ブロックの先回りの描き替え、VDP コマンドの予算つき小分け）。
- ROM のデータバンクの読み出し `src/core/rom.c`、素材の生成と詰め込み `tools/gen_title.py` / `tools/gen_city.py` /
  `tools/pack_assets.py`、総合検証 `tools/test/run_game_m2.sh`。
- **M1: 1 面の空中戦**（`GAME.ROM`）。夕焼けの空と 3 層の雲海（ラスターでの多重スクロール）、自機 Infinitas、
  Tick / Tock / Notch、敵弾、Quanta、∞ゲージ（THRUST / SPLIT / RAY / LIMIT BREAK）、HUD 帯、被弾・残機・GAME OVER。
- スプライト管理 `src/core/spr.c`（96 行で切り替える 2 帯分割、属性表の二重化、用途別グループ）と入力 `src/core/input.c`。
- スプライトと∞ゲージの絵を生成する `tools/gen_sprites.py` / `tools/gen_hud.py`、総合検証 `tools/test/run_game_m1.sh`。
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

### Fixed
- GHOST の待ち行列が溢れたコマンドを黙って捨て、描き戻しが抜けていた。
- タイトルの素材の読み込みがスプライトの表まで消し、上の帯の敵が黒く点滅していた。
- スプライトの絵の表が取り込んだファイルごとに複製されていた（4.8KB）。
