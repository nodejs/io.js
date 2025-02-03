'use strict';
require('../common');
const fixtures = require('../common/fixtures');
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const { test } = require('node:test');
const cwd = fixtures.path('test-runner', 'default-behavior');
const env = { ...process.env, 'NODE_DEBUG': 'test_runner' };

test('default concurrency', async () => {
  const args = ['--test'];
  const cp = spawnSync(process.execPath, args, { cwd, env });
  assert.match(cp.stderr.toString(), /concurrency: true,/);
});

test('concurrency of one', async () => {
  const args = ['--test', '--test-concurrency=1'];
  const cp = spawnSync(process.execPath, args, { cwd, env });
  assert.match(cp.stderr.toString(), /concurrency: 1,/);
});

test('concurrency of two', async () => {
  const args = ['--test', '--test-concurrency=2'];
  const cp = spawnSync(process.execPath, args, { cwd, env });
  assert.match(cp.stderr.toString(), /concurrency: 2,/);
});

test('isolation=none uses a concurrency of one', async () => {
  const args = ['--test', '--test-isolation=none'];
  const cp = spawnSync(process.execPath, args, { cwd, env });
  assert.match(cp.stderr.toString(), /concurrency: 1,/);
});

test('isolation=none overrides --test-concurrency', async () => {
  const args = [
    '--test', '--test-isolation=none', '--test-concurrency=2',
  ];
  const cp = spawnSync(process.execPath, args, { cwd, env });
  assert.match(cp.stderr.toString(), /concurrency: 1,/);
});
