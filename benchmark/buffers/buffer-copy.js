'use strict';
const common = require('../common.js');

const bench = common.createBenchmark(main, {
  bytes: [8, 128, 1024],
  partial: ['true', 'false'],
  n: [6e6],
});

function main({ n, bytes, partial }) {
  const source = Buffer.allocUnsafe(bytes);
  const target = Buffer.allocUnsafe(bytes);
  const sourceStart = (partial === 'true' ? Math.floor(bytes / 2) : 0);
  bench.start();
  for (let i = 0; i < n; i++) {
    source.copy(target, 0, sourceStart);
  }
  bench.end(n);
}
