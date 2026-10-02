#!/usr/bin/env python3
"""M0-3 PCM 方式の比較: run_m0_pcm.sh が記録した D/A 書き込み(時刻 値)を集計する。

  usage: check_pcm.py <作業ディレクトリ>

指標(試験区間 2 秒):
  出力数/秒  … 1 秒あたりの D/A 書き込み数(理想 7867)
  間隔 平均/標準偏差/最大 … 書き込み間隔(µs。理想 127.1)
  ±25% 外   … 理想間隔から 25% 以上ずれた間隔の割合(ジッタの目安)
  欠け      … 理想の 1.5 倍以上空いた間隔の数(サンプルが出なかった区間)
  CPU 残量   … 方式なし・分割なしの基準に対する平均アイドル(%)

合否(終了コード): 方式 C(p3_*)が 出力数/秒 >= 7800、間隔の標準偏差 < 30us、最大間隔 < 400us、
刻みの張り直し(rearm)が増えていないこと。方式 A/B は参考値(欠けるのが期待どおり)。
"""
import os, re, sys, statistics

T = 2.0 / 15734.264 * 1e6      # 理想の間隔(µs)

def main(d):
    fail = []
    rearm0 = None
    stats = {}
    for l in open(os.path.join(d, 'm0_pcm.log')):
        kv = dict(re.findall(r'(\w+)=(\d+)', l))
        stats[l.split()[0]] = kv
    print(f"{'設定':8s} {'CPU残量%':>8s} {'遅れ':>4s} {'出力数/秒':>9s} {'間隔平均':>8s} {'標準偏差':>8s} {'最大':>7s} {'±25%外':>7s} {'欠け':>5s}")
    for tag in sorted(stats):
        kv = stats[tag]
        p = os.path.join(d, tag + '.da')
        ts = [float(l.split()[0]) for l in open(p)] if os.path.exists(p) else []
        if len(ts) < 2:
            print(f"{tag:8s} {kv['pct']:>8s} {kv['late']:>4s} {'-':>9s}")
            continue
        iv = [(b - a) * 1e6 for a, b in zip(ts, ts[1:])]
        dur = ts[-1] - ts[0]
        off = sum(1 for x in iv if abs(x - T) > 0.25 * T) / len(iv) * 100
        gaps = sum(1 for x in iv if x > 1.5 * T)
        print(f"{tag:8s} {kv['pct']:>8s} {kv['late']:>4s} {len(ts)/dur:9.0f} {statistics.mean(iv):8.1f} "
              f"{statistics.pstdev(iv):8.1f} {max(iv):7.0f} {off:6.1f}% {gaps:5d}")
        if tag.startswith('p3_'):
            ok = len(ts) / dur >= 7800 and statistics.pstdev(iv) < 30 and max(iv) < 400
            if not ok: fail.append(tag)
    ra = [int(stats[t].get('rearm', 0)) for t in sorted(stats) if t.startswith('p3_')]
    if ra and ra[-1] != int(stats[sorted(stats)[0]].get('rearm', 0)):
        fail.append('rearm')
    print('判定: ' + ('OK(方式 C)' if not fail else 'NG ' + ' '.join(fail)))
    return 1 if fail else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
