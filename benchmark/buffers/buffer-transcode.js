'use strict';
const common = require('../common.js');
const assert = require('node:assert');
const buffer = require('node:buffer');

const hasIntl = !!process.config.variables.v8_enable_i18n_support;
const encodings = ['latin1', 'ascii', 'ucs2', 'utf8'];

if (!hasIntl) {
  console.log('Skipping: `transcode` is only available on platforms that support i18n`');
  process.exit(0);
}

const bench = common.createBenchmark(main, {
  fromEncoding: encodings,
  toEncoding: encodings,
  length: [1, 10, 1000],
  n: [1e5],
}, {
  combinationFilter(p) {
    return !(p.fromEncoding === 'ucs2' && p.toEncoding === 'utf8');
  },
});

function main({ n, fromEncoding, toEncoding, length }) {
  const input = Buffer.from('a'.repeat(length));
  let out = 0;
  bench.start();
  for (let i = 0; i < n; i++) {
    const dest = buffer.transcode(input, fromEncoding, toEncoding);
    out += dest.buffer.byteLength;
  }
  bench.end(n);
  assert.ok(out >= 0);
}
