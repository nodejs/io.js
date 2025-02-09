'use strict';

const common = require('../common.js');
const assert = require('assert');

const options = {
  flags: ['--expose-internals'],
};

const bench = common.createBenchmark(main, {
  n: [1024 * 1024 * 16],
}, options);

function main({ n }) {
  const { getAllowUnauthorized } = require('internal/options');

  const length = 1024;
  const array = new Array(length).fill(false);

  bench.start();

  for (let i = 0; i < n; ++i) {
    const index = i % length;
    array[index] = getAllowUnauthorized();
  }

  bench.end(n);

  // Verify the entries to prevent dead code elimination from making
  // the benchmark invalid.
  for (let i = 0; i < length; ++i) {
    assert.strictEqual(typeof array[i], 'boolean');
  }
}
