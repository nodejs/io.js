'use strict';

const common = require('../common.js');
const { createHash } = require('crypto');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  n: [1e5],
});

function main({ n }) {
  const array = [];
  for (let i = 0; i < n; ++i) {
    array.push(null);
  }
  bench.start();
  for (let i = 0; i < n; ++i) {
    array[i] = createHash('sha1');
  }
  bench.end(n);
  assert.strictEqual(typeof array[n - 1], 'object');
}
