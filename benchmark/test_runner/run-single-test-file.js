'use strict';
const common = require('../common');

const tmpdir = require('../../test/common/tmpdir');
const { run } = require('node:test');
const { writeFileSync, mkdirSync } = require('node:fs');
const { join } = require('node:path');

const fixtureContent = "const test = require('node:test'); test('test has ran');";

function makeTestDirWithFiles(dirPath, count) {
  mkdirSync(dirPath);
  for (let i = 0; i < count; i++) {
    writeFileSync(join(dirPath, `test-${i}.js`), fixtureContent);
  }
}

function getTestDirPath(numberOfTestFiles) {
  return join(tmpdir.path, `${numberOfTestFiles}-tests`);
}

function setup(numberOfTestFiles) {
  tmpdir.refresh();
  const dirPath = getTestDirPath(numberOfTestFiles);
  makeTestDirWithFiles(dirPath, numberOfTestFiles);
}

/**
 * This benchmark evaluates the overhead of running a single test file under different
 * isolation modes.
 * Specifically, it compares the performance of running tests in the
 * same process versus creating multiple processes.
 */
const bench = common.createBenchmark(main, {
  numberOfTestFiles: [1, 10, 100],
  isolation: ['none', 'process'],
}, {
  // We don't want to test the reporter here
  flags: ['--test-reporter=./benchmark/fixtures/empty-test-reporter.js'],
});

async function runBenchmark({ numberOfTestFiles, isolation }) {
  const dirPath = getTestDirPath(numberOfTestFiles);
  const stream = run({
    cwd: dirPath,
    isolation,
    concurrency: false, // We don't want to run tests concurrently
  });

  // eslint-disable-next-line no-unused-vars
  for await (const _ of stream);

  return numberOfTestFiles;
}

function main(params) {
  setup(params.numberOfTestFiles);
  bench.start();
  runBenchmark(params).then(() => {
    bench.end(1);
  });
}
