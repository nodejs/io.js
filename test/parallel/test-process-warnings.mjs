import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { describe, it } from 'node:test';
import assert from 'node:assert';

const fixturePath = fixtures.path('disable-warning.js');
const fixturePathWorker = fixtures.path('disable-warning-worker.js');
const dep1Message = /\(node:\d+\) \[DEP1\] DeprecationWarning/;
const dep2Message = /\(node:\d+\) \[DEP2\] DeprecationWarning/;
const experimentalWarningMessage = /\(node:\d+\) ExperimentalWarning/;

describe('process warnings', { concurrency: !process.env.TEST_PARALLEL }, () => {

  it('should emit all warnings by default', async () => {
    const { stdout, stderr, code, signal } = await spawnPromisified(process.execPath, [
      fixturePath,
    ]);

    assert.strictEqual(stdout, '');
    assert.match(stderr, dep1Message);
    assert.match(stderr, dep2Message);
    assert.match(stderr, experimentalWarningMessage);
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  describe('--no-warnings', { concurrency: !process.env.TEST_PARALLEL }, () => {
    it('should silence all warnings by default', async () => {
      const { stdout, stderr, code, signal } = await spawnPromisified(process.execPath, [
        '--no-warnings',
        fixturePath,
      ]);

      assert.strictEqual(stdout, '');
      assert.doesNotMatch(stderr, dep1Message);
      assert.doesNotMatch(stderr, dep2Message);
      assert.doesNotMatch(stderr, experimentalWarningMessage);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });
  });

  describe('--no-deprecation', { concurrency: !process.env.TEST_PARALLEL }, () => {
    it('should silence all deprecation warnings', async () => {
      const { stdout, stderr, code, signal } = await spawnPromisified(process.execPath, [
        '--no-deprecation',
        fixturePath,
      ]);

      assert.strictEqual(stdout, '');
      assert.doesNotMatch(stderr, dep1Message);
      assert.doesNotMatch(stderr, dep2Message);
      assert.match(stderr, experimentalWarningMessage);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });
  });

  describe('--disable-warning', { concurrency: !process.env.TEST_PARALLEL }, () => {
    it('should silence deprecation warning DEP1', async () => {
      const { stdout, stderr, code, signal } = await spawnPromisified(process.execPath, [
        '--disable-warning=DEP1',
        fixturePath,
      ]);

      assert.strictEqual(stdout, '');
      assert.doesNotMatch(stderr, dep1Message);
      assert.match(stderr, dep2Message);
      assert.match(stderr, experimentalWarningMessage);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('should silence deprecation warnings DEP1 and DEP2', async () => {
      const { stdout, stderr, code, signal } = await spawnPromisified(process.execPath, [
        '--disable-warning=DEP1',
        '--disable-warning=DEP2',
        fixturePath,
      ]);

      assert.strictEqual(stdout, '');
      assert.doesNotMatch(stderr, dep1Message);
      assert.doesNotMatch(stderr, dep2Message);
      assert.match(stderr, experimentalWarningMessage);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('should silence all deprecation warnings using type DeprecationWarning', async () => {
      const { stdout, stderr, code, signal } = await spawnPromisified(process.execPath, [
        '--disable-warning=DeprecationWarning',
        fixturePath,
      ]);

      assert.strictEqual(stdout, '');
      assert.doesNotMatch(stderr, dep1Message);
      assert.doesNotMatch(stderr, dep2Message);
      assert.match(stderr, experimentalWarningMessage);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('should silence all experimental warnings using type ExperimentalWarning', async () => {
      const { stdout, stderr, code, signal } = await spawnPromisified(process.execPath, [
        '--disable-warning=ExperimentalWarning',
        fixturePath,
      ]);

      assert.strictEqual(stdout, '');
      assert.match(stderr, dep1Message);
      assert.match(stderr, dep2Message);
      assert.doesNotMatch(stderr, experimentalWarningMessage);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('should pass down option to worker', async () => {
      const { stdout, stderr, code, signal } = await spawnPromisified(process.execPath, [
        '--disable-warning=DEP2',
        fixturePathWorker,
      ]);

      assert.strictEqual(stdout, '');
      assert.match(stderr, dep1Message);
      assert.doesNotMatch(stderr, dep2Message);
      assert.match(stderr, experimentalWarningMessage);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('should not support a comma separated list', async () => {
      const { stdout, stderr, code, signal } = await spawnPromisified(process.execPath, [
        '--disable-warning=DEP1,DEP2',
        fixturePathWorker,
      ]);

      assert.strictEqual(stdout, '');
      assert.match(stderr, dep1Message);
      assert.match(stderr, dep2Message);
      assert.match(stderr, experimentalWarningMessage);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('should be specifiable in NODE_OPTIONS', async () => {
      const { stdout, stderr, code, signal } = await spawnPromisified(process.execPath, [
        fixturePath,
      ], {
        env: {
          ...process.env,
          NODE_OPTIONS: '--disable-warning=DEP2'
        }
      });

      assert.strictEqual(stdout, '');
      assert.match(stderr, dep1Message);
      assert.doesNotMatch(stderr, dep2Message);
      assert.match(stderr, experimentalWarningMessage);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });
  });
});
