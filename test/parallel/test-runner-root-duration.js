'use strict';
const { spawnPromisified } = require('../common');
const fixtures = require('../common/fixtures');
const { strictEqual } = require('node:assert');
const { test } = require('node:test');

test('root duration is longer than test duration', async () => {
  const {
    code,
    stderr,
    stdout,
  } = await spawnPromisified(process.execPath, [
    fixtures.path('test-runner/root-duration.mjs'),
  ]);

  strictEqual(code, 0);
  strictEqual(stderr, '');
  const durations = [...stdout.matchAll(/duration_ms:? ([.\d]+)/g)];
  strictEqual(durations.length, 2);
  const testDuration = Number.parseFloat(durations[0][1]);
  const rootDuration = Number.parseFloat(durations[1][1]);
  strictEqual(Number.isNaN(testDuration), false);
  strictEqual(Number.isNaN(rootDuration), false);
  strictEqual(rootDuration >= testDuration, true);
});
