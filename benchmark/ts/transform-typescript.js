'use strict';

const common = require('../common');
const path = require('path');
const assert = require('node:assert');

const js = path.resolve(__dirname, '../fixtures/transform-types-benchmark.js');
const ts = path.resolve(__dirname, '../fixtures/transform-types-benchmark.ts');

const bench = common.createBenchmark(main, {
  filepath: [js, ts],
  n: [1e4],
}, {
  flags: ['--experimental-transform-types', '--disable-warning=ExperimentalWarning'],
});

async function main({ n, filepath }) {
  let output;
  bench.start();
  for (let i = 0; i < n; i++) {
    const { result } = await import(`${filepath}?${i}`);
    output = result;
  }
  bench.end(n);
  assert.ok(output);
}
