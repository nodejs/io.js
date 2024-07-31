'use strict';
const common = require('../common.js');
const assert = require('node:assert');

const bench = common.createBenchmark(main, {
  size: [16, 32, 64, 128],
  n: [1e6],
});

function main({ n, size }) {
  const input = btoa('A'.repeat(size));
  let out = 0;

  bench.start();
  for (let i = 0; i < n; i++) {
    out += atob(input).length;
  }
  bench.end(n);
  assert(out > 0);
}
