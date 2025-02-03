'use strict';
require('../common');
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const { test } = require('node:test');
const fixtures = require('../common/fixtures');

test('ensures --enable-source-maps does not throw an error', () => {
  const fixture = fixtures.path('test-runner', 'coverage', 'stdin.test.js');
  const args = ['--enable-source-maps', fixture];

  const result = spawnSync(process.execPath, args);

  assert.strictEqual(result.stderr.toString(), '');
  assert.strictEqual(result.status, 0);
});
