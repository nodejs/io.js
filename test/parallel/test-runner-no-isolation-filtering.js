'use strict';
require('../common');
const fixtures = require('../common/fixtures');
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const { test } = require('node:test');

const fixture1 = fixtures.path('test-runner', 'no-isolation', 'one.test.js');
const fixture2 = fixtures.path('test-runner', 'no-isolation', 'two.test.js');

test('works with --test-only', () => {
  const args = [
    '--test',
    '--test-reporter=tap',
    '--test-isolation=none',
    '--test-only',
    fixture1,
    fixture2,
  ];
  const child = spawnSync(process.execPath, args);
  const stdout = child.stdout.toString();

  assert.strictEqual(child.status, 0);
  assert.strictEqual(child.signal, null);
  assert.match(stdout, /# tests 2/);
  assert.match(stdout, /# suites 2/);
  assert.match(stdout, /# pass 2/);
  assert.match(stdout, /ok 1 - suite one/);
  assert.match(stdout, /ok 1 - suite one - test/);
  assert.match(stdout, /ok 2 - suite two/);
  assert.match(stdout, /ok 1 - suite two - test/);
});

test('works without --test-only', () => {
  const args = [
    '--test',
    '--test-reporter=tap',
    '--test-isolation=none',
    fixture1,
    fixture2,
  ];
  const child = spawnSync(process.execPath, args);
  const stdout = child.stdout.toString();

  assert.strictEqual(child.status, 0);
  assert.strictEqual(child.signal, null);
  assert.match(stdout, /# tests 2/);
  assert.match(stdout, /# suites 2/);
  assert.match(stdout, /# pass 2/);
  assert.match(stdout, /ok 1 - suite one/);
  assert.match(stdout, /ok 1 - suite one - test/);
  assert.match(stdout, /ok 2 - suite two/);
  assert.match(stdout, /ok 1 - suite two - test/);
});

test('works with --test-name-pattern', () => {
  const args = [
    '--test',
    '--test-reporter=tap',
    '--test-isolation=none',
    '--test-name-pattern=/test one/',
    fixture1,
    fixture2,
  ];
  const child = spawnSync(process.execPath, args);
  const stdout = child.stdout.toString();

  assert.strictEqual(child.status, 0);
  assert.strictEqual(child.signal, null);
  assert.match(stdout, /# tests 0/);
  assert.match(stdout, /# suites 0/);
});

test('works with --test-skip-pattern', () => {
  const args = [
    '--test',
    '--test-reporter=tap',
    '--test-isolation=none',
    '--test-skip-pattern=/one/',
    fixture1,
    fixture2,
  ];
  const child = spawnSync(process.execPath, args);
  const stdout = child.stdout.toString();

  assert.strictEqual(child.status, 0);
  assert.strictEqual(child.signal, null);
  assert.match(stdout, /# tests 1/);
  assert.match(stdout, /# suites 1/);
  assert.match(stdout, /# pass 1/);
  assert.match(stdout, /ok 1 - suite two - test/);
});
