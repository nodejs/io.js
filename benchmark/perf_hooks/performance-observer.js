'use strict';

const assert = require('assert');
const common = require('../common.js');

const {
  PerformanceObserver,
  performance,
} = require('perf_hooks');

function randomFn() {
  return Math.random();
}

const bench = common.createBenchmark(main, {
  n: [1e6],
  pending: [1, 10],
}, {
  options: ['--expose-internals'],
});

let _result;

function fillQueue(timerfied, pending) {
  for (let i = 0; i < pending; i++) {
    _result = timerfied();
  }
  // Avoid V8 deadcode (elimination)
  assert.ok(_result);
}

function main({ n, pending }) {
  const timerfied = performance.timerify(randomFn);

  let count = 0;
  const obs = new PerformanceObserver((entries) => {
    count += entries.getEntries().length;

    if (count >= n) {
      bench.end(count);
    } else {
      fillQueue(timerfied, pending);
    }
  });
  obs.observe({ entryTypes: ['function'], buffered: true });

  bench.start();
  fillQueue(timerfied, pending);
}
