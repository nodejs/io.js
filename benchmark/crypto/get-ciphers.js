'use strict';

const common = require('../common.js');

const bench = common.createBenchmark(main, {
  n: [1, 500000],
  v: ['crypto', 'tls'],
});

function main({ n, v }) {
  const method = require(v).getCiphers;
  let i = 0;
  // First call to getCiphers will dominate the results
  if (n > 1) {
    for (; i < n; i++)
      method();
  }
  bench.start();
  for (i = 0; i < n; i++) method();
  bench.end(n);
}
