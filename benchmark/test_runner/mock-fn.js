'use strict';

const common = require('../common');
const assert = require('node:assert');
const { test } = require('node:test');

const bench = common.createBenchmark(main, {
  n: [1e6],
  mode: ['define', 'execute'],
}, {
  // We don't want to test the reporter here
  flags: ['--test-reporter=./benchmark/fixtures/empty-test-reporter.js'],
});

const noop = () => {};

function benchmarkDefine(n) {
  let noDead;
  test((t) => {
    bench.start();
    for (let i = 0; i < n; i++) {
      noDead = t.mock.fn(noop);
    }
    bench.end(n);
    assert.ok(noDead);
  });
}

function benchmarkExecute(n) {
  let noDead;
  test((t) => {
    const mocked = t.mock.fn(noop);
    bench.start();
    for (let i = 0; i < n; i++) {
      noDead = mocked();
    }
    bench.end(n);
    assert.strictEqual(noDead, noop());
  });
}

function main({ n, mode }) {
  if (mode === 'define') {
    benchmarkDefine(n);
  } else if (mode === 'execute') {
    benchmarkExecute(n);
  }
}
