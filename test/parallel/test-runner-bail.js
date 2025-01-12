'use strict';

require('../common');
const fixtures = require('../common/fixtures');
const { describe, it } = require('node:test');
const { spawnSync } = require('node:child_process');
const assert = require('node:assert');

const bailTestFixtures = fixtures.path('test-runner/bailout');

describe('node:test bail in watch mode', () => {

  it('should not bail out if --test-bail is not set', () => {
    const child = spawnSync(
      process.execPath,
      [
        '--test',
        '--test-reporter=spec',
        '--test-concurrency=1',
      ],
      {
        cwd: bailTestFixtures,
      });

    assert.strictEqual(child.stderr.toString(), '');

    const output = child.stdout.toString();

    assert.match(output, /tests 4/);
    assert.match(output, /suites 2/);
    assert.match(output, /pass 1/);
    assert.match(output, /fail 3/);
    assert.match(output, /cancelled 0/);
  });

  it('should bail out on the first failing test', () => {
    const child = spawnSync(
      process.execPath,
      [
        '--test',
        '--test-bail',
        '--test-reporter=spec',
        '--test-concurrency=1',
      ],
      {
        cwd: bailTestFixtures,
      });

    assert.strictEqual(child.stderr.toString(), '');

    const output = child.stdout.toString();

    assert.match(output, /tests 3/);
    assert.match(output, /suites 1/);
    assert.match(output, /pass 0/);
    assert.match(output, /fail 1/);
    assert.match(output, /cancelled 2/);
  });

  it('should bail out on the first failing test with isolation none', () => {
    const child = spawnSync(
      process.execPath,
      [
        '--test',
        '--test-bail',
        '--test-reporter=spec',
        '--test-concurrency=1',
        '--test-isolation=none',
      ],
      {
        cwd: bailTestFixtures,
      });

    assert.strictEqual(child.stderr.toString(), '');

    const output = child.stdout.toString();

    assert.match(output, /tests 4/);
    assert.match(output, /suites 2/);
    assert.match(output, /pass 0/);
    assert.match(output, /fail 1/);
    assert.match(output, /cancelled 3/);
  });
});
