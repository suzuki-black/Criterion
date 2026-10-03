# Criterion

MSX turboR 専用（FS-A1ST / FS-A1GT、R800 DRAM モード）の横スクロールシューティング。開発中。
設計は [docs/指示書.md](docs/指示書.md)、進捗は [docs/M0_技術検証.md](docs/M0_技術検証.md)、
[docs/M1_横エンジン基盤.md](docs/M1_横エンジン基盤.md)、[docs/M2_縦切り1面.md](docs/M2_縦切り1面.md)、
[docs/M3_ROTOR_GHOST.md](docs/M3_ROTOR_GHOST.md)、[docs/M4_重力_高速_多段ラスター.md](docs/M4_重力_高速_多段ラスター.md)、[docs/M5_巨大戦艦_モード分割.md](docs/M5_巨大戦艦_モード分割.md)、[docs/M6_最終面_エンディング.md](docs/M6_最終面_エンディング.md) を参照。

現在（M6）: 起動ロゴ → タイトル → SPACE で出撃 → 1 面（都市・LEGION・PLUMB）→ 2 面「重力井戸」→ 3 面「臨界加速」→
4 面「層流」→ 5 面「規格戦艦ノルム」→ 6 面「日時計要塞」→ 7 面「校正室」（過去のボスの再校正版と LIBRA）→
8 面「原器」（PROTOTYPE ZERO の 3 形態と脱出）→ エンディング → 2 周目まで、画面を消さずにつながる（1 周クリアが可能。音は未実装）。

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
