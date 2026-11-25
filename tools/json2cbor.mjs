// api.php の JSON 応答 (または C キーのダンプ。api キーの中身を使う) を ?fmt=bin と同じ CBOR に変換する。
// MCU 版・ホスト版 (impl/) の入力に使う。
// fit がなければ null にする (サーバが推定できなかったときと同じ。サーバ側のツールで付ける)
// usage: node tools/json2cbor.mjs in.json out.cbor
import { readFileSync, writeFileSync } from 'node:fs';

let data = JSON.parse(readFileSync(process.argv[2], 'utf8'));
if (data.api) data = data.api;

const out = [];
const head = (major, n) => {
  if (n < 24) out.push((major << 5) | n);
  else if (n < 0x100) out.push((major << 5) | 24, n);
  else if (n < 0x10000) out.push((major << 5) | 25, n >> 8, n & 255);
  else out.push((major << 5) | 26, (n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255);
};
const uint = (n) => head(0, n);
const int = (n) => (n >= 0 ? head(0, n) : head(1, -1 - n));
const nil = () => out.push(0xf6);
const array = (items) => { head(4, items.length); for (const f of items) f(); };
const text = (s) => { const b = Buffer.from(s, 'utf8'); head(3, b.length); out.push(...b); };
const bytes = (b) => { head(2, b.length); for (const x of b) out.push(x); };
const map = (entries) => { head(5, entries.length); for (const [k, f] of entries) { text(k); f(); } };

const u16le = (arr) => { const b = new Uint8Array(arr.length * 2); arr.forEach((v, i) => { b[2 * i] = v & 255; b[2 * i + 1] = (v >> 8) & 255; }); return b; };
const plane = (p) => () => map([['width', () => uint(p.width)], ['height', () => uint(p.height)], ['pixels', () => bytes(u16le(p.pixels))]]);
const od = (o) => () => {
  const hits = o && o.hits ? o.hits : [];
  const b = new Uint8Array(hits.length * 6);
  hits.forEach(([u, v, rgb], i) => {
    const uu = Math.max(0, Math.min(65535, Math.round(u * 65535))), vv = Math.max(0, Math.min(65535, Math.round(v * 65535)));
    b.set([uu & 255, uu >> 8, vv & 255, vv >> 8, rgb & 255, (rgb >> 8) & 255], 6 * i);
  });
  map([['count', () => uint(hits.length)], ['hits', () => bytes(b)]]);
};

// 推定結果を CBOR 用の小さな形にする (api.php の compactFit と同じ)
const KINDS = ['through', 'stopping', 'inside', 'contained'];
const HIDE_REASONS = [null, 'none', 'lit', 'corr', 'amb', 'od'];
const fit = (f) => () => {
  if (!f) return nil();
  const track = ['start', 'wallEntry', 'end', 'exit'].flatMap((k) => f[k].map((v) => Math.round(v * 65536)));
  const hide = HIDE_REASONS.indexOf(f.hideReason), kind = KINDS.indexOf(f.kind);
  map([
    ['hide', () => uint(hide < 0 ? 1 : hide)],
    ['kind', () => uint(kind < 0 ? 0 : kind)],
    ['corr', () => int(Math.round(f.corr * 1000))],
    ['track', () => array(track.map((v) => () => int(v)))],
    ['fitMs', () => uint(Math.round(f.stats?.timeMs ?? 0))],
  ]);
};

map([
  ['version', () => uint(data.version ?? 0x0101)],
  ['statusCode', () => uint(data.statusCode ?? 0)],
  ['errorMsg', () => text(data.errorMsg ?? '')],
  ['timestamp', () => uint(data.timestamp ?? 0)],
  ['pixelsCrc32', () => uint(data.pixelsCrc32 ?? 0)],
  ['planes', () => map([['top', plane(data.planes.top)], ['side', plane(data.planes.side)], ['bottom', plane(data.planes.bottom)]])],
  ['od', () => map([['top', od(data.od?.top)], ['side', od(data.od?.side)], ['bottom', od(data.od?.bottom)]])],
  ['fit', fit(data.fit)],
]);
writeFileSync(process.argv[3], Buffer.from(out));
console.log(`${process.argv[3]}: ${out.length} bytes`);
