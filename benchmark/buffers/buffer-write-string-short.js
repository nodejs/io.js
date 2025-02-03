'use strict';

const common = require('../common.js');
const bench = common.createBenchmark(main, {
  encoding: [
    'utf8', 'ascii', 'latin1',
  ],
  len: [1, 8, 16, 32],
  n: [1e6],
});

function main({ len, n, encoding }) {
  const buf = Buffer.allocUnsafe(len);
  const string = Buffer.from('a'.repeat(len)).toString();
  bench.start();
  for (let i = 0; i < n; ++i) {
    buf.write(string, 0, encoding);
  }
  bench.end(n);
}
