'use strict';
const common = require('../common.js');

const bench = common.createBenchmark(main, {
  type: ['URL', 'URLSearchParams'],
  n: [1e3, 1e6],
});

function main({ type, n }) {
  const params = type === 'URL' ?
    new URL('https://nodejs.org').searchParams :
    new URLSearchParams();

  bench.start();
  for (let i = 0; i < n; i++) {
    params.append('test', i);
  }
  bench.end(n);
}
