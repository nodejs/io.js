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
  observe: ['function'],
});

let _result;

function main({ n, observe }) {
  const obs = new PerformanceObserver(() => {
    bench.end(n);
  });
  obs.observe({ entryTypes: [observe], buffered: true });

  const timerfied = performance.timerify(randomFn);

  bench.start();
  for (let i = 0; i < n; i++)
    _result = timerfied();

  // Avoid V8 deadcode (elimination)
  assert.ok(_result);
}
