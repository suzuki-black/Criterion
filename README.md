# Criterion

MSX turboR 専用（FS-A1ST / FS-A1GT、R800 DRAM モード）の横スクロールシューティング。開発中。
設計は [docs/指示書.md](docs/指示書.md)、進捗は [docs/M0_技術検証.md](docs/M0_技術検証.md)、
[docs/M1_横エンジン基盤.md](docs/M1_横エンジン基盤.md)、[docs/M2_縦切り1面.md](docs/M2_縦切り1面.md)、
[docs/M3_ROTOR_GHOST.md](docs/M3_ROTOR_GHOST.md)、[docs/M4_重力_高速_多段ラスター.md](docs/M4_重力_高速_多段ラスター.md)、[docs/M5_巨大戦艦_モード分割.md](docs/M5_巨大戦艦_モード分割.md)、[docs/M6_最終面_エンディング.md](docs/M6_最終面_エンディング.md)、[docs/M7_サウンド.md](docs/M7_サウンド.md) を参照。後回しにした点は [docs/TODO.md](docs/TODO.md)。

現在（M7）: 起動ロゴ → タイトル → SPACE で出撃 → 1〜8 面 → エンディング → 2 周目まで、画面を消さずにつながる（1 周クリアが可能）。
MSX-MUSIC（FM 6 声 + リズム）と PSG の BGM 21 曲、PCM のオーケストラヒットと音声、効果音が鳴る。

| 操作 | キーボード | ジョイスティック |
|---|---|---|
| 移動 | カーソルキー | 方向 |
| A（ショット、押しっぱなしで連射） | SPACE / Z | トリガ 1 |
| B（∞ゲージの確定） | X / M | トリガ 2 |

タイトルで ↑↑↓↓←→←→ に続けてキーボードの B、A を押すと、隠しの設定に入る（無敵・BGM を聴く・始める面。
上下で項目、左右で値。SPACE で BGM を鳴らす・EXIT で抜ける）。7・8 面は前の面の続きを前提にしているので、始める面は 1〜6 から選ぶのが確実。

本作はオリジナル作品であり、実在の作品・企業とは関係ありません。

## ビルド

```sh
make            # GAME.ROM（本体、1MB ASCII8）と build/m0_*.rom（技術検証）
```

openMSX での起動（turboR の実 BIOS の機械定義）:

```sh
openmsx -machine My_FS-A1ST_512K -carta GAME.ROM -romtype ASCII8
```

必要なもの: SDCC 4.x、node、（検証用）openMSX と Python 3 + Pillow。

## ライセンス

MIT（[LICENSE](LICENSE)）
