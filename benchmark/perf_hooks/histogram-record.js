'use strict';

const assert = require('assert');
const common = require('../common.js');

const { createHistogram } = require('perf_hooks');

const bench = common.createBenchmark(main, {
  n: [1e5],
});

function main({ n }) {
  const histogram = createHistogram();
  bench.start();
  for (let i = 0; i < n; i++) {
    histogram.record(i + 1);
    /* eslint-disable no-unused-expressions */
    histogram.max;
    histogram.mean;
    /* eslint-enable no-unused-expressions */
  }
  bench.end(n);

  // Avoid V8 deadcode (elimination)
  assert.ok(histogram);
}
