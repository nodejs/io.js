'use strict';

const assert = require('assert');
const common = require('../common.js');

const bench = common.createBenchmark(main, {
  n: [1e6],
});

function main({ n }) {
  const arr = [];
  for (let i = 0; i < n; ++i) {
    arr.push(performance.now());
  }

  bench.start();
  for (let i = 0; i < n; i++) {
    arr[i] = performance.now();
  }
  bench.end(n);

  assert.strictEqual(arr.length, n);
}
