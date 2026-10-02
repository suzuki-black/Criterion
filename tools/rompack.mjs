// rompack.mjs — .ihx(常駐コード) + 任意のバンク(コード/データ) → MegaROM(ASCII8)
//  出典: ZeroNoHoukouR/tools/rompack.mjs を Criterion 用に --size 対応へ拡張
//
//  ROM レイアウト(このプロジェクトの規律。ここで機械的に強制する):
//    bank0-3  ROM 0x00000-0x07FFF  常駐コード(.ihx の 0x4010-0xBFFF)  ← 上限 32KB。超過=エラー
//             (Criterion は起動時にページ 1-2 = 0x4000-0xBFFF を RAM へ複製して実行する: sys_ram_exec)
//    bank4-   ROM 0x08000-        データ ← 各 8KB
//
//  使い方:
//    node tools/rompack.mjs --code build/rom.ihx --out GAME.ROM [--bank N file] ...
//      --bank N file : 8KB バンク N(4 以降) に file を配置。
//                      .ihx(0xA000リンクの bcall コード)なら 0xA000 起点で展開、
//                      .bin(生データ)ならそのまま先頭から配置。
//
//  --size N      : ROM の大きさ(既定 1MB)
//
//  出力の最後に「常駐コード使用量／残り」「各バンク使用量」を表示(空き容量の可視化)。

import { readFileSync, writeFileSync, existsSync } from 'node:fs';

let   ROM_SIZE    = 0x100000;  // 既定 1MB(128バンク, 指示書 8.12)。--size で変更可(M0 テスト ROM 等)
const BANK_SIZE   = 0x2000;    // 8KB
const CODE_BASE   = 0x4000;    // 常駐コードのリンク基準アドレス
const CODE_LIMIT  = 0x8000;    // 常駐コード ROM オフセット上限(=32KB=bank0-3)。Criterion は起動時に 0x4000-0xBFFF を RAM へ複製する
const ASSET_FIRST = 4;         // データ/バンクコードは bank4 以降

// ---- 引数パース ----
const args = process.argv.slice(2);
let inIhx = null, outRom = null;
const banks = [];              // { n, file }         … 1バンク以内のコード/データ
const assets = [];             // { n, file }         … 複数バンクにまたがる大アセット(YJK画像等)
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--code') inIhx = args[++i];
  else if (a === '--out') outRom = args[++i];
  else if (a === '--bank') { const n = parseInt(args[++i], 10); banks.push({ n, file: args[++i] }); }
  else if (a === '--size') ROM_SIZE = parseInt(args[++i]);
  else if (a === '--asset') { const n = parseInt(args[++i], 10); assets.push({ n, file: args[++i] }); }
  else { console.error(`ERROR: 不明な引数: ${a}`); process.exit(2); }
}
if (!inIhx || !outRom) {
  console.error('usage: node tools/rompack.mjs --code <in.ihx> --out <out.rom> [--bank N file]...');
  process.exit(2);
}

const rom = Buffer.alloc(ROM_SIZE, 0xFF);
const used = new Array(ROM_SIZE / BANK_SIZE).fill(0);   // バンク毎の最終使用オフセット(表示用)

// ---- .ihx を ROM オフセットへ展開する共通関数 ----
function loadIhx(path, linkBase, romBase, spanLimit, label) {
  let max = -1;
  for (const l of readFileSync(path, 'utf8').split(/\r?\n/)) {
    if (l[0] !== ':') continue;
    const n = parseInt(l.substr(1, 2), 16);
    const a = parseInt(l.substr(3, 4), 16);
    const t = parseInt(l.substr(7, 2), 16);
    if (t !== 0) continue;                       // データレコードのみ
    for (let i = 0; i < n; i++) {
      const rel = a + i - linkBase;
      if (rel < 0 || rel >= spanLimit) {
        console.error(`ERROR: ${label} がリンク範囲外: addr=0x${(a + i).toString(16)} (base=0x${linkBase.toString(16)}, span=0x${spanLimit.toString(16)})`);
        process.exit(2);
      }
      rom[romBase + rel] = parseInt(l.substr(9 + i * 2, 2), 16);
      if (rel > max) max = rel;
    }
  }
  return max + 1;   // バイト数
}

// ---- 常駐コード(bank0-3) ----
const codeLen = loadIhx(inIhx, CODE_BASE, 0x0000, CODE_LIMIT, '常駐コード');
if (codeLen > CODE_LIMIT) {
  console.error(`ERROR: 常駐コードが ${CODE_LIMIT / 1024}KB を超過(${codeLen}B)。`);
  process.exit(2);
}
for (let n = 0; n * BANK_SIZE < codeLen; n++) used[n] = Math.min(codeLen - n * BANK_SIZE, BANK_SIZE);

// ---- 追加バンク(コード/データ) ----
for (const { n, file } of banks) {
  if (!Number.isInteger(n) || n < 0 || n >= ROM_SIZE / BANK_SIZE) {
    console.error(`ERROR: バンク番号が範囲外: ${n}`); process.exit(2);
  }
  if (n < ASSET_FIRST) { console.error(`ERROR: bank${n} は常駐コード予約(bank0-3)。データ/コードは bank${ASSET_FIRST}+ へ。`); process.exit(2); }
  if (!existsSync(file)) { console.error(`ERROR: --bank ${n} のファイルが無い: ${file}`); process.exit(2); }
  const romBase = n * BANK_SIZE;
  let len;
  if (file.endsWith('.ihx')) {
    len = loadIhx(file, 0xA000, romBase, BANK_SIZE, `bank${n} コード`);
  } else {
    const buf = readFileSync(file);
    if (buf.length > BANK_SIZE) { console.error(`ERROR: bank${n} のデータが 8KB 超: ${buf.length}B (${file})`); process.exit(2); }
    buf.copy(rom, romBase);
    len = buf.length;
  }
  used[n] = Math.max(used[n], len);
}

// ---- 大アセット(複数バンク連続配置) ----
// YJKタイトル画等、8KBを超える生データを bankN から連続バンクへ跨って敷き詰める。
// 読み出し側は bankN,N+1,… を 8KB窓でめくって VRAM へ流す(vdp_blit_bank_vram)。
for (const { n, file } of assets) {
  if (!Number.isInteger(n) || n < ASSET_FIRST || n >= ROM_SIZE / BANK_SIZE) {
    console.error(`ERROR: --asset のバンク番号が範囲外(${ASSET_FIRST}..${ROM_SIZE / BANK_SIZE - 1}): ${n}`); process.exit(2);
  }
  if (!existsSync(file)) { console.error(`ERROR: --asset ${n} のファイルが無い: ${file}`); process.exit(2); }
  const buf = readFileSync(file);
  const romBase = n * BANK_SIZE;
  if (romBase + buf.length > ROM_SIZE) {
    console.error(`ERROR: --asset ${n} が ROM 末尾を超過: ${buf.length}B @ bank${n} (末尾まで ${ROM_SIZE - romBase}B)`); process.exit(2);
  }
  const span = Math.ceil(buf.length / BANK_SIZE);
  for (let k = 0; k < span; k++) {
    if (used[n + k] > 0) { console.error(`ERROR: --asset ${n}(${file}) が使用済み bank${n + k} と衝突`); process.exit(2); }
  }
  buf.copy(rom, romBase);
  for (let k = 0; k < span; k++) {
    const inThis = Math.min(BANK_SIZE, buf.length - k * BANK_SIZE);
    used[n + k] = Math.max(used[n + k], inThis);
  }
  console.log(`  asset ${file}: ${buf.length}B → bank${n}..${n + span - 1} (${span}バンク)`);
}

writeFileSync(outRom, rom);

// ---- 空き容量レポート ----
const KB = (b) => (b / 1024).toFixed(1);
console.log(`ROM: ${outRom}  (MegaROM ASCII8, ${ROM_SIZE / 1024}KB / ${ROM_SIZE / BANK_SIZE} banks)`);
console.log(`  常駐コード(bank0-3): ${codeLen}B / ${CODE_LIMIT}B  残り ${CODE_LIMIT - codeLen}B (${KB(CODE_LIMIT - codeLen)}KB)`);
for (let n = ASSET_FIRST; n < used.length; n++) {
  if (used[n] > 0) console.log(`  bank${String(n).padStart(2)}          : ${used[n]}B / 8192B  残り ${BANK_SIZE - used[n]}B`);
}
const freeBanks = used.slice(ASSET_FIRST).filter((u) => u === 0).length;
console.log(`  空きバンク: ${freeBanks} / ${used.length - ASSET_FIRST}  (= ${KB(freeBanks * BANK_SIZE)}KB 未使用)`);
