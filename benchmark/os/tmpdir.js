'use strict';

const common = require('../common.js');
const { tmpdir } = require('os');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  n: [1e6],
});

function main({ n }) {
  // Warm up.
  const length = 1024;
  const array = [];
  for (let i = 0; i < length; ++i) {
    array.push(tmpdir());
  }

  bench.start();
  for (let i = 0; i < n; ++i) {
    const index = i % length;
    array[index] = tmpdir();
  }
  bench.end(n);

  // Verify the entries to prevent dead code elimination from making
  // the benchmark invalid.
  for (let i = 0; i < length; ++i) {
    assert.strictEqual(typeof array[i], 'string');
  }
}
