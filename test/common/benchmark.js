'use strict';

const assert = require('assert');
const fork = require('child_process').fork;
const path = require('path');

const runjs = path.join(__dirname, '..', '..', 'benchmark', 'run.js');

function runBenchmark(name, env) {
  const argv = ['test'];

  argv.push(name);

  const mergedEnv = { ...process.env, ...env };

  const child = fork(runjs, argv, {
    env: mergedEnv,
    stdio: ['inherit', 'pipe', 'inherit', 'ipc'],
  });
  child.stdout.setEncoding('utf8');

  let stdout = '';
  child.stdout.on('data', (line) => {
    stdout += line;
  });

  child.on('exit', (code, signal) => {
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);

    // This bit makes sure that each benchmark file is being sent settings such
    // that the benchmark file runs just one set of options. This helps keep the
    // benchmark tests from taking a long time to run. Therefore, stdout should be composed as follows:
    // The first and last lines should be empty.
    // Each test should be separated by a blank line.
    // The first line of each test should contain the test's name.
    // The second line of each test should contain the configuration for the test.
    // If the test configuration is not a group, there should be exactly two lines.
    // Otherwise, it is possible to have more than two lines.

    const splitTests = stdout.split(/\n\s*\n/);

    for (let testIdx = 1; testIdx < splitTests.length - 1; testIdx++) {
      const lines = splitTests[testIdx].split('\n');
      assert.ok(/.+/.test(lines[0]));

      if (!lines[1].includes('group="')) {
        assert.strictEqual(lines.length, 2, `benchmark file not running exactly one configuration in test: ${stdout}`);
      }
    }
  });
}

module.exports = runBenchmark;
