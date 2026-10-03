# assets/pcm — PCM の素材の出典

どれも Freesound の **Creative Commons 0（CC0 1.0）**。著作権の放棄に相当し、商用を含めて自由に使え、表記も不要。
ここでは出典として記録しておく。`tools/snd_pcm.py` が切り出して 7.99kHz・8 ビットに変換する（元のファイルは加工しない）。

| ファイル | 用途 | 作者 | 出典 | ライセンス |
|---|---|---|---|---|
| 90741__bigdumbweirdo__orch-hit.wav | オーケストラヒット（P01〜P03） | BigDumbWeirdo | https://freesound.org/s/90741/ | CC0 1.0 |
| 82249__kevoy__acoustic-snare.wav | スネア（P05） | KEVOY | https://freesound.org/people/KEVOY/sounds/82249/ | CC0 1.0 |
| 23127__john-rayson__reverse-cymbal.wav | 逆再生シンバル（P04） | John Rayson | https://freesound.org/s/23127/ | CC0 1.0 |
| 587886__samsterbirdies__explosion-large.flac | 大爆発（P06）の元 | SamsterBirdies | https://freesound.org/people/SamsterBirdies/sounds/587886/ | CC0 1.0 |
| 587886__samsterbirdies__explosion-large.wav | 上の FLAC を macOS の `afconvert -f WAVE -d LEI16` で WAV にしたもの | — | — | CC0 1.0 |

- 2026-10-04 にユーザーが選んで取得（候補の中からスネアは差し替え）。
- 往年のサンプラーや既存のレコードからの切り出しではないこと（指示書 6.4）を、各ページの説明で確かめた
  （オケヒは楽器の録音を重ねて作ったもの、爆発は爆竹の録音を加工したもの、スネアとシンバルは実物の録音）。
- 声（V01〜V06）は素材を使わず、`tools/snd_pcm.py` の自作のフォルマント合成。
