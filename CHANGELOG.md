# Changelog

形式は [Keep a Changelog](https://keepachangelog.com/ja/1.1.0/)、バージョンは [SemVer](https://semver.org/lang/ja/) に従う。

## [Unreleased]

### Fixed
- PCM の刻み: 帯で R#23 が変わると鎖が切れた（表示ラインで数える）。刻み方式の間に本体が R#1 を書くと VBLANK 割込みが戻って
  刻みが 1 フレームに 1 つになった。刻み方式をやめたフレームの JIFFY を 2 回数えた。
- 処理落ちのフレームに自機の弾だけ進み、小さな的（TOLERANCE の砲台など）をすり抜けた（処理落ちのフレームは弾も止める）。
- 短冊の無い面で空のブロックが描き替えられず、前の面の絵（縦穴の壁、YJK のバイト）が残った。
- 面の開始表示の帯が、カメラの高さによってページ 2 の別の行を見せていた（帯の R#23 を固定）。
- 世界の位置が 16384 ドットで 0 に戻り、5 面の途中で列の計算が壊れた（差分で進め、面ごとに戻す）。
- 画面分割を始めたフレームに VRAM の窓を 370 ライン待っていた（表示中のリストの分割だけを見る）。
- 4 面の雲の描き戻しの位置が地図の始まり（`g_w0`）の分ずれていた。

### Changed
- オーバレイの窓を 0x1000-0x37FF に（CODE0 を広げて選曲・効果音・PCM を置く）。本体から PCM 方式 B（M0 の比較用）を外した。
- `run_game_m1〜m6.sh` は PCM を切って流す（エンジンの時間を見る。PCM は `run_game_m7.sh`）。
- M1 の検証の Quanta: 撃ち落とした敵の Quanta を拾えるかが時間割しだいだったので、1 個を自機へ置いて拾わせ、
  （8 で一周する）カーソルではなく `armed` を見る。
- 面の土台（`stage.c`）を共通化（短冊・地図・上の空・面の開始表示・`ovl_bands`）。列の窓を左右に動かす（逆走）、遅れたら
  1 コマンド 12 ブロック。面の開始表示の文字列は写して持つ（古いオーバレイの文字列が読み込みで消えていた）。
- ラスターの ISR は、6 ライン未満の次の項目を割込みを待たずに続けて適用する（近い項目でフレームの残りが消えていた）。
- VRAM の配置: スプライトの表をページ 3 へ、HUD をページ 2 へ（表示リングの 256 行をすべてプレイエリアに）。
- 雲の描画を asm と表引きに（起動と再開が約 3 秒 → 約 1 秒）。
- VDP コマンドの発行を 5 本ずつ 3 回に分けた（割込み禁止を 1 ライン未満に）。
- 直接割込みでは R#15 を常に 1 にし、FH の判定を IN 1 回にした。
- PCM の刻み 244 では、ラスター項目の適用を先、穴埋めを最後にした。

### Added
- **M7: サウンド**。MSX-MUSIC 6 声 + リズム + PSG の BGM ドライバ（`src/game/snd.c`、中核は asm。テンポの外部制御・移調・
  ピッチベンド一括・シーケンスの中の PCM）、曲 21 本（`tools/songs.py` の MML を `tools/gen_sound.py` が翻訳）、PCM 12 本
  （オケヒ・シンバル・爆発・音声。`tools/snd_pcm.py` で合成）、場面に合わせた選曲と音の演出・PSG の効果音（`src/game/music.c`）。
  総合検証 `tools/test/run_game_m7.sh`。気になる点は `docs/TODO.md`。
- **M6: 7〜8 面とエンディング**。7 面「校正室」（白い方眼の測定空間、過去のボス 6 体の再校正版を試験として連戦、
  天秤 LIBRA）、8 面「原器」（縦穴、歪曲回廊、TOLERANCE、PROTOTYPE ZERO の 3 形態、脱出）、終幕（SCREEN 12 の地球、
  スタッフロール、∞）、2 周目（弾と連射とボスの振る舞いが変わる）。1 周クリアが可能。総合検証 `tools/test/run_game_m6.sh`。
- **M5: 5〜6 面**。5 面「規格戦艦ノルム」（接近の 8 段の拡大と閃光、機銃・主砲塔、HANGAR DECK、艦底、R#23 の帯で
  上下するプレス、GAUGE BLOCK、撃沈）、6 面「日時計要塞」（上 48 ラインの SCREEN 12 の惑星と画面分割、ROTOR の歯車、
  影の帯、HOURGLASS、文字盤の砲台、LINE で描く GNOMON の針）。総合検証 `tools/test/run_game_m5.sh`。
- **M4: 2〜4 面**。面ごとのコードのオーバレイ（`src/include/ovl.h`、ROM のバンク 104 から、ページ 0 の RAM へ 512 バイト/フレーム）。
  2 面「重力井戸」（重力・天地のロール・COUNTERWEIGHT・KILOGRAM と重力レンズ）、3 面「臨界加速」（符号つきのスクロール速度、
  加速・急停止・逆走、METRONOME、TACHOMETER）、4 面「層流」（12 帯の多段ラスタースクロール、陽炎帯、VERNIER・RULER・STRATUM）。
  総合検証 `tools/test/run_game_m4.sh`（起動から 4 面の終わりまで、遅れ 0）。
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
