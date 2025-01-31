'use strict';
require('../common');
const fixtures = require('../common/fixtures');
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const { test } = require('node:test');
const cwd = fixtures.path('test-runner', 'default-behavior');
const env = { ...process.env, 'NODE_DEBUG': 'test_runner' };

test('default timeout -- Infinity', async () => {
  const args = ['--test'];
  const cp = spawnSync(process.execPath, args, { cwd, env });
  assert.match(cp.stderr.toString(), /timeout: Infinity,/);
});

test('timeout of 10ms', async () => {
  const args = ['--test', '--test-timeout', 10];
  const cp = spawnSync(process.execPath, args, { cwd, env });
  assert.match(cp.stderr.toString(), /timeout: 10,/);
});

test('isolation=none uses the --test-timeout flag', async () => {
  const args = [
    '--test', '--test-isolation=none', '--test-timeout=10',
  ];
  const cp = spawnSync(process.execPath, args, { cwd, env });
  assert.match(cp.stderr.toString(), /timeout: 10,/);
});
