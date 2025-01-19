'use strict';

require('../common');
const fixtures = require('../common/fixtures');
const { describe, it, beforeEach } = require('node:test');
const { spawnSync } = require('node:child_process');
const assert = require('node:assert');
const tmpdir = require('../common/tmpdir.js');

describe('node:test bail', () => {
  beforeEach(() => {
    tmpdir.refresh();
  });

  it('should run all tests when --test-bail is not set', () => {
    const child = spawnSync(
      process.execPath,
      [
        '--test',
        '--test-reporter=spec',
        fixtures.path('test-runner', 'bailout', 'sequential', 'test.mjs'),
      ],
    );

    assert.strictEqual(child.stderr.toString(), '');
    const output = child.stdout.toString();

    assert.doesNotMatch(output, /Bail out!/);
    assert.match(output, /tests 3/);
    assert.match(output, /suites 1/);
    assert.match(output, /pass 0/);
    assert.match(output, /fail 3/);
    assert.match(output, /cancelled 0/);
  });

  it('should bail on sequential single file', () => {
    const child = spawnSync(
      process.execPath,
      [
        '--test',
        '--test-bail',
        '--test-reporter=spec',
        fixtures.path('test-runner', 'bailout', 'sequential', 'test.mjs'),
      ],
    );

    assert.strictEqual(child.stderr.toString(), '');
    const output = child.stdout.toString();

    assert.match(output, /Bail out!/);
    assert.match(output, /tests 3/);
    assert.match(output, /suites 1/);
    assert.match(output, /pass 0/);
    assert.match(output, /fail 1/);
    assert.match(output, /cancelled 2/);
  });

  it('should bail on sequential with isolation none', () => {
    const child = spawnSync(
      process.execPath,
      [
        '--test',
        '--test-bail',
        '--test-reporter=spec',
        '--test-isolation=none',
        fixtures.path('test-runner', 'bailout', 'sequential', 'test.mjs'),
      ],
    );

    assert.strictEqual(child.stderr.toString(), '');
    const output = child.stdout.toString();

    assert.match(output, /Bail out!/);
    assert.match(output, /tests 3/);
    assert.match(output, /suites 1/);
    assert.match(output, /pass 0/);
    assert.match(output, /fail 1/);
    assert.match(output, /cancelled 2/);
  });

  it('should bail on parallel files with different loading times', () => {
    const child = spawnSync(
      process.execPath,
      [
        '--test',
        '--test-bail',
        '--test-reporter=spec',
        '--test-concurrency=2',
        fixtures.path('test-runner', 'bailout', 'parallel-concurrency', 'first.mjs'),
        fixtures.path('test-runner', 'bailout', 'parallel-concurrency', 'second.mjs'),
      ],
      {
        env: {
          __TEST_SYNC_PATH__: tmpdir.path,
        }
      }
    );

    assert.strictEqual(child.stderr.toString(), '');
    const output = child.stdout.toString();

    assert.match(output, /Bail out!/);
    assert.match(output, /tests 3/);
    assert.match(output, /suites 1/);
    assert.match(output, /pass 1/);
    assert.match(output, /fail 1/);
    assert.match(output, /cancelled 1/);
  });

  it('should bail with limited concurrency and multiple files', () => {
    const child = spawnSync(
      process.execPath,
      [
        '--test',
        '--test-bail',
        '--test-reporter=spec',
        '--test-concurrency=2',
        fixtures.path('test-runner', 'bailout', 'parallel-concurrency', 'first.mjs'),
        fixtures.path('test-runner', 'bailout', 'parallel-concurrency', 'second.mjs'),
        fixtures.path('test-runner', 'bailout', 'parallel-concurrency', 'third.mjs'),
      ],
      {
        env: {
          __TEST_SYNC_PATH__: tmpdir.path,
        }
      }
    );

    assert.strictEqual(child.stderr.toString(), '');
    const output = child.stdout.toString();

    assert.match(output, /Bail out!/);
    assert.match(output, /tests 4/);
    assert.match(output, /suites 1/);
    assert.match(output, /pass 1/);
    assert.match(output, /fail 1/);
    assert.match(output, /cancelled 2/);
  });

  it('should execute hooks in correct order during bailout', () => {
    const child = spawnSync(
      process.execPath,
      [
        '--test',
        '--test-bail',
        '--test-reporter=spec',
        fixtures.path('test-runner', 'bailout', 'hooks-order', 'test.mjs'),
      ],
    );

    assert.strictEqual(child.stderr.toString(), '');
    const output = child.stdout.toString();

    const hooksOrderMatch = output.match(/HOOKS_ORDER: (.*)/);
    assert(hooksOrderMatch, 'Hooks order output should be present');
    const hooksOrder = hooksOrderMatch[1].split(',');

    assert.deepStrictEqual(hooksOrder, [
      'before',
      'beforeEach',
      'test1',
      'afterEach',
      'after',
    ]);

    assert.match(output, /Bail out!/);
    assert.match(output, /tests 2/);
    assert.match(output, /suites 1/);
    assert.match(output, /pass 0/);
    assert.match(output, /fail 1/);
    assert.match(output, /cancelled 1/);
  });

  describe('validation', () => {
    it('should not allow bail with watch mode', () => {
      const child = spawnSync(
        process.execPath,
        ['--test', '--test-bail', '--watch'],
      );

      assert.match(child.stderr.toString(),
                   /The property 'options\.bail' bail is not supported while watch mode is enabled/);
    });
  });

  // TODO(pmarchini): Bailout is not supported in watch mode yet but it should be.
  // We should enable this test once it is supported.
  it.todo('should handle the bail option with watch mode');
});
