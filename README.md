# Criterion

MSX turboR 専用（FS-A1ST / FS-A1GT、R800 DRAM モード）の横スクロールシューティング。開発中。
設計は [docs/指示書.md](docs/指示書.md)、進捗は [docs/M0_技術検証.md](docs/M0_技術検証.md)、
[docs/M1_横エンジン基盤.md](docs/M1_横エンジン基盤.md)、[docs/M2_縦切り1面.md](docs/M2_縦切り1面.md) を参照。

現在（M2）: 起動ロゴ → タイトル → SPACE で出撃 → 1 面（空中戦・降下・都市・中ボス・ボス・上昇）→ 2 面の空中戦まで、
画面を消さずにつながる（中ボスとボスは仮）。

| 操作 | キーボード | ジョイスティック |
|---|---|---|
| 移動 | カーソルキー | 方向 |
| A（ショット、押しっぱなしで連射） | SPACE / Z | トリガ 1 |
| B（∞ゲージの確定） | X / M | トリガ 2 |

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
