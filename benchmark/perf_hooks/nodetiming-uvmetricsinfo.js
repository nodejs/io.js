'use strict';

const common = require('../common.js');
const assert = require('node:assert');
const fs = require('node:fs/promises');

const {
  performance,
} = require('perf_hooks');

const bench = common.createBenchmark(main, {
  n: [1e6],
  events: [1, 1000, 10000],
});

async function runEvents(events) {
  for (let i = 0; i < events; ++i) {
    assert.ok(await fs.statfs(__filename));
  }
}

async function main({ n, events }) {
  await runEvents(events);
  bench.start();
  for (let i = 0; i < n; i++) {
    assert.ok(performance.nodeTiming.uvMetricsInfo);
  }
  bench.end(n);
}
