'use strict';
const common = require('../common.js');
const { posix } = require('path');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  path: [
    '',
    '.',
    '/tmp/bar',
    '/home/node/..',
    posix.join(__dirname, '/..'),
    'bar/baz',
  ],
  n: [1e5],
});

function main({ n, path }) {
  bench.start();
  let a;
  for (let i = 0; i < n; i++) {
    a = posix.toNamespacedPath(path);
  }
  bench.end(n);
  assert.ok(a + 'a');
}
