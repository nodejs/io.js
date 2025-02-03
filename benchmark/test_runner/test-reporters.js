'use strict';

const common = require('../common');
const { run } = require('node:test');
const reporters = require('node:test/reporters');
const { Readable } = require('node:stream');
const assert = require('node:assert');

const bench = common.createBenchmark(main, {
  n: [1e4],
  reporter: Object.keys(reporters),
});

// No need to run this for every benchmark,
// it should always be the same data.
const stream = run({
  files: ['../fixtures/basic-test-runner.js'],
});
let testResults;

async function main({ n, reporter: r }) {
  testResults ??= await stream.toArray();

  // Create readable streams for each iteration
  const readables = Array.from({ length: n }, () => Readable.from(testResults));

  // Get the selected reporter
  const reporter = reporters[r];

  bench.start();

  let noDead;
  for (const readable of readables) {
    // Process each readable stream through the reporter
    noDead = await readable.compose(reporter).toArray();
  }

  bench.end(n);

  assert.ok(noDead);
}
