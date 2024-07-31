'use strict';
const common = require('../common.js');
const { win32 } = require('path');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  path: [
    '',
    'c:/ignore',
    'd:\\a/b\\c/d',
    '\\e.exe',
    'c:/blah\\blah',
    'd:/games',
    'c:../a',
    win32.join(__dirname, '/..'),
  ],
  n: [1e5],
});

function main({ n, path }) {
  bench.start();
  let a;
  for (let i = 0; i < n; i++) {
    a = win32.toNamespacedPath(path);
  }
  bench.end(n);
  assert.ok(a + 'a');
}
