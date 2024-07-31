'use strict';

const assert = require('assert');
const common = require('../common.js');

const bench = common.createBenchmark(main, {
  n: [1e6],
  method: ['timeOrigin', 'toJSON'],
});

function main({ method, n }) {
  switch (method) {
    case 'timeOrigin':
      benchTimeOrigin(n);
      break;
    case 'toJSON':
      benchToJSON(n);
      break;
    default:
      throw new Error(`Unsupported method ${method}`);
  }
}

function benchTimeOrigin(n) {
  const arr = [];
  for (let i = 0; i < n; ++i) {
    arr.push(performance.timeOrigin);
  }

  bench.start();
  for (let i = 0; i < n; i++) {
    arr[i] = performance.timeOrigin;
  }
  bench.end(n);

  assert.strictEqual(arr.length, n);
}

function benchToJSON(n) {
  bench.start();
  for (let i = 0; i < n; i++) {
    performance.toJSON();
  }
  bench.end(n);
}
