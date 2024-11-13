'use strict';

const common = require('../common.js');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  method: ['withoutdefaults', 'withdefaults'],
  n: [1e8],
});

function oldStyleDefaults(x, y) {
  x ||= 1;
  y ||= 2;
  assert.strictEqual(x, 1);
  assert.strictEqual(y, 2);
}

function defaultParams(x = 1, y = 2) {
  assert.strictEqual(x, 1);
  assert.strictEqual(y, 2);
}

function runOldStyleDefaults(n) {
  bench.start();
  for (let i = 0; i < n; i++)
    oldStyleDefaults();
  bench.end(n);
}

function runDefaultParams(n) {
  bench.start();
  for (let i = 0; i < n; i++)
    defaultParams();
  bench.end(n);
}

function main({ n, method }) {
  switch (method) {
    case 'withoutdefaults':
      runOldStyleDefaults(n);
      break;
    case 'withdefaults':
      runDefaultParams(n);
      break;
    default:
      throw new Error(`Unexpected method "${method}"`);
  }
}
