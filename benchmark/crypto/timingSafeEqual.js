'use strict';

const common = require('../common.js');
const assert = require('node:assert');
const { randomBytes, timingSafeEqual } = require('node:crypto');

const bench = common.createBenchmark(main, {
  n: [1e5],
  bufferSize: [10, 100, 200, 2_100, 22_023],
});

function main({ n, bufferSize }) {
  const bufs = [randomBytes(bufferSize), randomBytes(bufferSize)];
  bench.start();
  let count = 0;
  for (let i = 0; i < n; i++) {
    const ret = timingSafeEqual(bufs[i % 2], bufs[1]);
    if (ret) count++;
  }
  bench.end(n);
  assert.strictEqual(count, Math.floor(n / 2));
}
