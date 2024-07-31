'use strict';

const common = require('../common.js');
const { randomBytes } = require('crypto');

// Add together with imports
const assert = require('assert');

let _cryptoResult;

const bench = common.createBenchmark(main, {
  size: [64, 1024, 8 * 1024, 16 * 1024],
  n: [1e5],
});

function main({ n, size }) {
  bench.start();
  for (let i = 0; i < n; ++i)
    _cryptoResult = randomBytes(size);
  bench.end(n);
  // Avoid V8 deadcode (elimination)
  assert.ok(_cryptoResult);
}
