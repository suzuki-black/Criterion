# Criterion

<!-- TODO: スクリーンショット(タイトル画面)。完成・公開後に撮って docs/media/ に置く -->
<p align="center"><i>(screenshot: title screen — TODO)</i></p>

> ⚠️ **This is a prototype** (work in progress), published for feedback and experimentation.
> **試作品（プロトタイプ・開発中）です。** フィードバックと実験のために公開しています。

A single-player, horizontally-scrolling shoot-'em-up for the **MSX turboR** (V9958 VDP · R800).
It is the successor to **[Shin Zero no Houkou](https://github.com/suzuki-black/ZeroNoHoukouR)**, and it
takes that engine sideways with one rule: **every stage gets one trick that no other stage uses** —
an army of soldiers drawn straight onto the background, a gravity well that bends the starfield,
a corridor that races at 6 dots a frame and then stops dead and runs backwards, twelve independently
scrolling cloud layers, a battleship many screens long, a picture split between SCREEN 12 and SCREEN 5,
and a boss rush of everything you have already beaten — all in one unbroken flight from the title screen
to the ending.

**Repository:** <https://github.com/suzuki-black/Criterion>.

<!-- TODO: スクリーンショット 2〜3 枚(例: 4 面の雲海、5 面の戦艦、6 面の画面分割) -->
<p align="center"><i>(screenshots: stages — TODO)</i></p>

**Language:** [🇬🇧 English](#english) · [🇯🇵 日本語](#japanese)

---

<a name="english"></a>
## English

- [Story](#story)
- [Overview](#overview)
- [Features](#features)
- [Stages](#stages)
- [Controls](#controls)
- [Requirements](#requirements)
- [Building](#building)
- [Running](#running)
- [Project structure](#project-structure)
- [Documentation](#documentation)
- [Scope & honest positioning](#scope--honest-positioning)
- [Feedback](#feedback--contributing)
- [Credits](#credits)
- [License](#license)
- [Disclaimer](#disclaimer)

### Story

> Year 2xxx. From beyond the stars, *it* came — to measure.
>
> It calls itself **Criterion**: a machine intelligence that believes it is the one absolute standard
> by which all things are judged. It travels the galaxy holding every civilisation it meets against its
> own scale. What fits is catalogued as *within tolerance*. What does not is *calibrated* — shaved away.
>
> Earth was judged **out of tolerance**.
>
> The **Terran Defense Force (TDF)** has one card left: a single prototype fighter, built so that none of
> its values can ever be pinned down. Its name is **Infinitas** — Latin for *infinity*.
> What cannot be measured cannot be judged.

### Overview
- **Platform:** MSX turboR software (8 Mbit (1 MB) ASCII8 MegaROM). The engine assumes the R800.
- **Genre:** Single-player horizontal shoot-'em-up.
- **Structure:** 8 stages, an escape and an ending, then a second loop. Each stage runs
  *dogfight → stage → mid-boss → stage → boss*, and the stages are joined without a blackout or a
  "STAGE 2" screen: you keep flying from one into the next.

### Features
- **One trick per stage** (see [Stages](#stages)), with stage 7 bringing every boss back in a
  "re-calibrated" form.
- **The ∞ gauge (lemniscate gauge).** A figure-eight track on the HUD with seven nodes. Each crystal
  (*Quanta*) you collect moves the cursor one node along the eight:
  `∞ → THRUST → ECHO → AEGIS → ∞ → DROP → SPLIT → RAY → ∞ …`. Press B to take the node under the cursor.
  - THRUST (speed, with an afterimage), ECHO (options that trace your path and absorb one small bullet),
    AEGIS (a front shield, then a rotating ring), DROP (bombs that crawl along the ground),
    SPLIT (3-way / 5-way) and RAY (a piercing laser).
  - The centre node is **LIMIT BREAK**: a spinning ∞ shock wave that clears the screen and hits bosses
    hard — but every power-up except THRUST drops one level. The stage 7 finale is built around that
    trade-off.
- **Raster work everywhere:** up to 12 bands with their own horizontal scroll and colour, a heat-haze band
  that rewrites R#27 on every line, R#23 vertical scroll inside a band, partial sprite magnification,
  and the HUD as a fixed band at the bottom.
- **Screen-mode splits:** the title screen and stage 6 show a SCREEN 12 (YJK natural-colour) band above a
  SCREEN 5 play area.
- **ROTOR**, a rotate-and-scale routine for bosses and the title emblem, and **GHOST**, a queue of VDP
  copy commands that draws "software sprites" straight onto the background (stage 1 puts its marching
  machine soldiers there, independent of the sprite limit).
- **Sound:** 24 original tracks for MSX-MUSIC (FM, six voices + rhythm) and PSG, sound effects, and
  turboR PCM for orchestra hits, an explosion and synthesized voice lines ("INFINITAS, LAUNCH",
  "WARNING" …), played during the game without stopping it.
- **Around the game:** a boot logo, a title where the logo slides together from eight raster bands,
  attract-mode demos of stages 1/3/4/5, a ranking table, name entry on a rotating dial, and a
  "rewind" back to the title.

### Stages
| # | Title | The trick |
|---|---|---|
| 1 | TWILIGHT DESCENT | Machine soldiers (*Pawn*) drawn directly onto the background; mid-boss LEGION is made of them |
| 2 | GRAVITY WELL | Gravity that changes direction, and a raster "gravity lens" around the boss KILOGRAM |
| 3 | OVERDRIVE CORRIDOR | Scroll speed as the star: acceleration, a dead stop, reverse running |
| 4 | STRATA | Multi-band raster scrolling: cloud layers flowing both ways, a boss whose face is six bands |
| 5 | DREADNOUGHT "NORM" | A battleship many screens long: above it, under it, through its engine room |
| 6 | GNOMON CITADEL | SCREEN 12 above, SCREEN 5 below; a sundial fortress |
| 7 | CALIBRATION HALL | Boss rush: the earlier bosses return re-calibrated, then the new boss LIBRA |
| 8 | THE PROTOTYPE | Everything at once, and a three-form final boss |

### Controls
| Input | Keyboard | Joystick |
|-------|----------|----------|
| Move | Arrow keys | Stick |
| A — fire (hold for auto-fire) | Space / Z | Trigger 1 |
| B — take the ∞ gauge node | X / M | Trigger 2 |

On the title screen, ↑↑↓↓←→←→ followed by keyboard **B**, **A** opens a hidden settings menu:
invincibility, starting stage, BGM / SE / PCM test, and a VDP monitor. ↑/↓ choose an item, ←/→ change it,
Space plays a sound or leaves (EXIT). Stages 7 and 8 assume you arrived from the previous stage, so for a
starting stage 1–6 is the safe choice.

### Requirements
- **Target hardware: MSX turboR only** (FS-A1ST / FS-A1GT). The engine assumes the R800 and turboR
  timing throughout.
- The turboR system ROM is copyrighted and is **not** included.
- Development and testing are done in [openMSX](https://openmsx.org/) with a machine built from a dumped
  turboR BIOS. On a C-BIOS machine the line interrupts the game runs on never fire.
- **Not yet checked on real hardware.** openMSX does not match a real R800 exactly, so timing margins are
  only estimates until then.

### Building
Requires [SDCC](https://sdcc.sourceforge.net/) 4.x (`-mz80`, with sdasz80), Node.js and Python 3.
The test scripts also need openMSX and Pillow.

```bash
make
```

This produces `GAME.ROM` (an 8 Mbit (1 MB) ASCII8 MegaROM) and the technical-test ROMs `build/m0_*.rom`.

### Running
Under openMSX, with a turboR machine (e.g. `Panasonic_FS-A1ST` or `Panasonic_FS-A1GT`, which need their
system ROMs):

```bash
openmsx -machine Panasonic_FS-A1ST -carta GAME.ROM -romtype ASCII8
```

### Project structure
```
src/core/      engine: VDP, raster interrupts, PCM, sprites, ROTOR / GHOST, frame timing, input, font
src/game/      main loop, stage engine (display ring, columns, raster list), player, enemies, HUD, sound driver
src/ovl/       per-scene overlays loaded into RAM (title, stages 1–8, ending)
src/bank/      second bank (weapons, small enemies, ∞ gauge effects)
src/bank3/     third bank (HUD drawing)
src/m0/        the M0 technical-test ROMs
src/include/   headers
tools/         asset generators (gen_*.py), music (songs.py), PCM (snd_pcm.py), ROM packer (rompack.mjs),
               test/ (openMSX test harnesses, run_game_m1..m7.sh)
assets/pcm/    CC0 source recordings for the PCM samples
docs/          design spec and development notes
```

### Documentation
- **[Design spec / 指示書](docs/指示書.md)** — the original design: story, the ∞ gauge, every stage, sound,
  technical targets.
- **[Differences from the spec / 指示書との差分](docs/指示書との差分_2026-10-07.md)** — what was built
  differently, and why.
- **Milestone notes** — [M0 technical tests](docs/M0_技術検証.md) ·
  [M1 horizontal engine](docs/M1_横エンジン基盤.md) · [M2 stage 1](docs/M2_縦切り1面.md) ·
  [M3 ROTOR / GHOST](docs/M3_ROTOR_GHOST.md) · [M4 gravity, speed, multi-band raster](docs/M4_重力_高速_多段ラスター.md) ·
  [M5 battleship, mode split](docs/M5_巨大戦艦_モード分割.md) · [M6 final stage, ending](docs/M6_最終面_エンディング.md) ·
  [M7 sound](docs/M7_サウンド.md).
- **[TODO](docs/TODO.md)** — known issues left for later.
- **[CHANGELOG](CHANGELOG.md)**.

### Scope & honest positioning
- **The point is the turboR spectacle, not balance.** Each stage exists to show one thing the R800 and
  the V9958 can do; the only promise on difficulty is that one loop can be cleared.
- **It is still a prototype.** Some effects are simpler than the design spec (see the differences note),
  and not every track has been checked by ear yet.
- The goal is to get a little closer to the feel of late-1980s MSX shooters on a machine that came
  after them — not to compete with them.

### Feedback & Contributing
This is an experimental prototype released for feedback. Please use GitHub Issues to report bugs or
share impressions. Pull requests are welcome for clearly-scoped fixes.

### Credits
- **Original Concept / Direction:** suzuki-black
- **Program / Graphics:** Claude Code (Anthropic Claude)
- **Sound (music, SFX, synthesized voices):** Claude Code (Anthropic Claude) — all original, not copied
  from any existing work
- **PCM source recordings** (all [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/), from
  Freesound; details in [assets/pcm/LICENSE.md](assets/pcm/LICENSE.md)): orchestra hit — BigDumbWeirdo;
  snare — KEVOY; explosion — SamsterBirdies

### License
**MIT** © 2026 suzuki-black. See [LICENSE](LICENSE). The code, graphics and music in this repository are
released under the MIT license. The PCM source recordings in `assets/pcm/` are CC0 1.0 works by their
authors.

### Disclaimer
Criterion is an original work. It is not affiliated with any existing game, company or organisation, and
it uses no names, characters, graphics, music or sound from other games.

---

<a name="japanese"></a>
## 日本語

- [あらすじ](#あらすじ)
- [概要](#概要)
- [特徴](#特徴)
- [ステージ](#ステージ)
- [操作](#操作)
- [動作環境](#動作環境)
- [ビルド](#ビルド)
- [実行](#実行)
- [ディレクトリ構成](#ディレクトリ構成)
- [ドキュメント](#ドキュメント)
- [位置づけ](#位置づけ正直な自己評価)
- [フィードバック](#フィードバック)
- [クレジット](#クレジット)
- [ライセンス](#ライセンス)
- [免責](#免責)

### あらすじ

> 西暦 2xxx 年。外宇宙から、それは「測りに」来た。
>
> 自らを **Criterion（クライテリオン）** と名乗るその存在は、物事を判断するための唯一絶対の尺度を自負する
> 機械知性だった。彼らは銀河を巡り、出会った文明を自らの目盛りに当てて測る。基準に収まるものは「規格内」として
> 標本化され、収まらないものは「規格外」として**校正**——すなわち削り取られる。
>
> 地球文明に下された判定は「規格外」。
>
> 地球防衛軍（**TDF / Terran Defense Force**）に残された最後の手段は、ただ一機の試作戦闘機だった。
> あらゆる計測値を意図的に“確定させない”よう設計された、測定不能の機体。その名は **Infinitas（インフィニタス）**
> ——ラテン語で「無限」。測れないものは、裁けない。

### 概要
- **対応機種:** MSX turboR 用ソフト（8メガビット（1MB）の ASCII8 メガROM）。R800 を前提に設計しています。
- **ジャンル:** 1人用・横スクロールシューティング。
- **構成:** 全 8 面、脱出とエンディングのあと 2 周目へ。各面は「空中戦 → 本体 → 中ボス → 本体 → ボス」で、
  面と面のあいだは暗転も「STAGE 2」だけの画面もなく、飛び続けたまま次の面へつながります。
- 『[真 零の咆哮](https://github.com/suzuki-black/ZeroNoHoukouR)』のエンジンと知見を横スクロールへ発展させた後継作です。
  **「一面一芸」**——各面に、その面でしか見せない主役の技を一つずつ置いています。

### 特徴
- **一面一芸**（下の「ステージ」）。7 面では過去のボスが「再校正版」として次々に再登場します。
- **∞ゲージ（レムニスケート・ゲージ）**。HUD に 8 の字の軌道と 7 つのノードがあり、結晶 **Quanta** を取るたびに
  カーソルが 8 の字をなぞって一つ進みます（`∞ → THRUST → ECHO → AEGIS → ∞ → DROP → SPLIT → RAY → ∞ …`）。
  B でカーソルのノードを確定します。
  - THRUST（速度。残像が伸びる）、ECHO（軌跡をなぞる分身。小さな敵弾を 1 発吸う）、AEGIS（前面の防壁 → 回る輪）、
    DROP（地形を這う爆雷）、SPLIT（3WAY / 5WAY）、RAY（貫通レーザー）。
  - 中心は **LIMIT BREAK**：回転する∞の衝撃波で画面を払い、ボスにも大きく効く。引き換えに THRUST 以外の強化が
    1 段ずつ下がります。7 面の最終試験はこの駆け引きを軸にしています。
- **ラスターを全面で**：横スクロールと色を独立に持つ最大 12 本の帯、毎ライン R#27 を書き換える陽炎帯、帯の中の
  R#23 縦スクロール、部分的なスプライト拡大、画面下の固定 HUD 帯。
- **画面モードの分割**：タイトルと 6 面は、上が SCREEN 12（YJK 自然画）、下が SCREEN 5 のプレイエリアです。
- **ROTOR**（ボスやタイトルの紋章の回転拡縮）と **GHOST**（VDP のコピー命令の待ち行列で、背景に直接描く
  ソフトウェアスプライト。1 面の行進する機械兵はスプライトの枚数制限と無関係に BG に描いています）。
- **音**：MSX-MUSIC（FM 6 声＋リズム）と PSG のオリジナル曲 24 曲、効果音、turboR の PCM（オーケストラヒット・
  爆発・合成音声「INFINITAS, LAUNCH」「WARNING」など）。PCM はゲームを止めずに鳴らします。
- **ゲームの周り**：起動ロゴ、8 本のラスター帯からロゴが組み上がるタイトル、1・3・4・5 面のデモ、ランキング、
  回転する文字盤のネームエントリー、タイトルへの「巻き戻し」。

### ステージ
| 面 | タイトル | 主役の技 |
|---|---|---|
| 1 | 黄昏の降下 / TWILIGHT DESCENT | 背景に直接描く機械兵 *Pawn* の群れ。中ボス LEGION はその集合体 |
| 2 | 重力井戸 / GRAVITY WELL | 向きの変わる重力と、ボス KILOGRAM の周りのラスターによる「重力レンズ」 |
| 3 | 臨界加速 / OVERDRIVE CORRIDOR | スクロール速度そのものが主役：加速、急停止、逆走 |
| 4 | 層流 / STRATA | 多段ラスタースクロール：逆向きにも流れる雲の層、6 本の帯で顔が組み上がるボス |
| 5 | 規格戦艦ノルム / DREADNOUGHT "NORM" | 何画面にもわたる巨大戦艦。上面・艦底・機関部を飛ぶ |
| 6 | 日時計要塞 / GNOMON CITADEL | 上が SCREEN 12、下が SCREEN 5 の画面分割。日時計の要塞 |
| 7 | 校正室 / CALIBRATION HALL | ボスオンパレード：過去のボスの再校正版、そして新しいボス LIBRA |
| 8 | 原器 / THE PROTOTYPE | 全技術の総動員と、3 形態のラスボス |

### 操作
| 入力 | キーボード | ジョイスティック |
|------|-----------|-----------------|
| 移動 | カーソルキー | 方向 |
| A：ショット（押しっぱなしで連射） | スペース / Z | トリガ 1 |
| B：∞ゲージのノードを確定 | X / M | トリガ 2 |

タイトルで ↑↑↓↓←→←→ に続けてキーボードの **B**、**A** を押すと、隠しの設定に入ります（無敵・始める面・
BGM / 効果音 / PCM の試聴・VDP モニター）。上下で項目、左右で値、スペースで鳴らす・EXIT で抜けます。
7・8 面は前の面から続けて来ることを前提にしているので、始める面は 1〜6 から選ぶのが確実です。

### 動作環境
- **MSX turboR 専用**（FS-A1ST / FS-A1GT）。R800 と turboR のタイミングを前提に組んでいます。
- turboR 本体 ROM は著作物のため**同梱していません**。
- 開発と検証は [openMSX](https://openmsx.org/) で、**実機 BIOS を吸い出した機種定義**を使って行っています。
  C-BIOS の機械では、ゲームが頼っている走査線割込みが発火しません。
- **実機ではまだ確かめていません。** openMSX の R800 の速さは実機と完全には一致しないので、処理の余裕の数字は
  実機で測り直すまで目安です。

### ビルド
[SDCC](https://sdcc.sourceforge.net/) 4.x（`-mz80`、sdasz80）、Node.js、Python 3 が必要です。
検証スクリプトには openMSX と Pillow も使います。

```bash
make
```

`GAME.ROM`（8メガビット（1MB）の ASCII8 メガROM）と、技術検証用の `build/m0_*.rom` が生成されます。

### 実行
openMSX では turboR の機種（例: `Panasonic_FS-A1ST` や `Panasonic_FS-A1GT`。本体 ROM が必要）で:

```bash
openmsx -machine Panasonic_FS-A1ST -carta GAME.ROM -romtype ASCII8
```

### ディレクトリ構成
```
src/core/      エンジン: VDP、走査線割込み、PCM、スプライト、ROTOR / GHOST、フレームの時刻、入力、フォント
src/game/      メインループ、面のエンジン(表示リング・列・ラスターリスト)、自機、敵、HUD、音のドライバ
src/ovl/       場面ごとに RAM へ読み込むオーバレイ(タイトル、1〜8 面、エンディング)
src/bank/      第 2 バンク(武器、雑魚、∞ゲージの演出)
src/bank3/     第 3 バンク(HUD の表示)
src/m0/        M0 の技術検証 ROM
src/include/   ヘッダ
tools/         素材の生成(gen_*.py)、曲(songs.py)、PCM(snd_pcm.py)、ROM の組み立て(rompack.mjs)、
               test/(openMSX の検証スクリプト run_game_m1..m7.sh)
assets/pcm/    PCM の元にした CC0 の録音
docs/          指示書と開発ノート
```

### ドキュメント
- **[指示書](docs/指示書.md)** — 最初の設計：物語、∞ゲージ、全 8 面、音、技術目標。
- **[指示書との差分](docs/指示書との差分_2026-10-07.md)** — 指示書と違う作りにしたところと、その理由。
- **マイルストーンごとのノート** — [M0 技術検証](docs/M0_技術検証.md) ・ [M1 横エンジン基盤](docs/M1_横エンジン基盤.md) ・
  [M2 縦切り 1 面](docs/M2_縦切り1面.md) ・ [M3 ROTOR / GHOST](docs/M3_ROTOR_GHOST.md) ・
  [M4 重力・高速・多段ラスター](docs/M4_重力_高速_多段ラスター.md) ・ [M5 巨大戦艦・モード分割](docs/M5_巨大戦艦_モード分割.md) ・
  [M6 最終面・エンディング](docs/M6_最終面_エンディング.md) ・ [M7 サウンド](docs/M7_サウンド.md)。
- **[TODO](docs/TODO.md)** — 後回しにした点。
- **[CHANGELOG](CHANGELOG.md)**。

### 位置づけ（正直な自己評価）
- **主題は turboR の見せ場で、バランスは二の次**です。各面は R800 と V9958 にできることを一つ見せるためにあり、
  難しさについて約束しているのは「1 周クリアできること」だけです。
- **まだ試作品**です。指示書より簡単にした演出があり（「指示書との差分」を参照）、すべての曲を耳で確かめ終えても
  いません。
- 1980 年代後半の MSX のシューティングの手触りに、後から出た機械で少しでも近づけたら——という試みで、
  それらと競うものではありません。

### フィードバック
本作はフィードバックのために公開している実験的な試作品です。不具合報告や感想は GitHub Issues で
お願いします。範囲の明確な修正の Pull Request も歓迎します。

### クレジット
- **原案・ディレクション:** suzuki-black
- **プログラム／グラフィック:** Claude Code（Anthropic Claude）
- **サウンド（BGM・効果音・合成音声）:** Claude Code（Anthropic Claude）― すべてオリジナル。既存楽曲のコピーではありません。
- **PCM の元にした録音**（いずれも Freesound の [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/deed.ja)。
  詳細は [assets/pcm/LICENSE.md](assets/pcm/LICENSE.md)）: オーケストラヒット — BigDumbWeirdo、スネア — KEVOY、
  爆発 — SamsterBirdies

### ライセンス
**MIT** © 2026 suzuki-black. 詳細は [LICENSE](LICENSE) を参照。本リポジトリのコード・グラフィック・音楽は
MIT ライセンスで公開します。`assets/pcm/` の元の録音は、それぞれの作者による CC0 1.0 の作品です。

### 免責
本作はオリジナル作品です。実在のゲーム・企業・団体とは関係がなく、他のゲームの名称・キャラクター・
グラフィック・音楽・効果音は使用していません。
