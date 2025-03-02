import { spawnPromisified } from '../common/index.mjs';
import fixtures from '../common/fixtures.js';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';


const commonArgs = [
  '--input-type=module',
  '--eval',
];

describe('ESM: unsettled and rejected promises', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should exit for an unsettled TLA promise via --eval with a warning', async () => {
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      ...commonArgs,
      'await new Promise(() => {})',
    ]);

    assert.match(stderr, /Warning: Detected unsettled top-level await at.+\[eval1\]:1/);
    assert.match(stderr, /await new Promise/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 13);
  });

  it('should exit for an unsettled TLA promise via --eval without warning', async () => {
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      '--no-warnings',
      ...commonArgs,
      'await new Promise(() => {})',
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 13);
  });

  it('should throw for a rejected TLA promise via --eval', async () => {
    // Rejected TLA promise, --eval
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      ...commonArgs,
      'await Promise.reject(new Error("Xyz"))',
    ]);

    assert.match(stderr, /Error: Xyz/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
  });

  it('should exit for an unsettled TLA promise and respect explicit exit code via --eval', async () => {
    // Rejected TLA promise, --eval
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      ...commonArgs,
      'process.exitCode = 42;await new Promise(() => {})',
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 42);
  });

  it('should throw for a rejected TLA promise and ignore explicit exit code via --eval', async () => {
    // Rejected TLA promise, --eval
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      ...commonArgs,
      'process.exitCode = 42;await Promise.reject(new Error("Xyz"))',
    ]);

    assert.match(stderr, /Error: Xyz/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
  });

  it('should exit for an unsettled TLA promise with warning', async () => {
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      fixtures.path('es-modules/tla/unresolved.mjs'),
    ]);

    assert.match(stderr, /Warning: Detected unsettled top-level await at.+unresolved\.mjs:5\b/);
    assert.match(stderr, /await new Promise/);
    assert.strictEqual(stdout, 'the exit listener received code: 13\n');
    assert.strictEqual(code, 13);
  });

  it('should exit for an unsettled TLA promise without warning', async () => {
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      '--no-warnings',
      fixtures.path('es-modules/tla/unresolved.mjs'),
    ]);

    assert.deepStrictEqual({ code, stdout, stderr }, {
      code: 13,
      stdout: 'the exit listener received code: 13\n',
      stderr: '',
    });
  });

  it('should throw for a rejected TLA promise via stdin', async () => {
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      '--no-warnings',
      fixtures.path('es-modules/tla/rejected.mjs'),
    ]);

    assert.match(stderr, /Error: Xyz/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
  });

  it('should exit for an unsettled TLA promise and respect explicit exit code', async () => {
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      '--no-warnings',
      fixtures.path('es-modules/tla/unresolved-withexitcode.mjs'),
    ]);

    assert.deepStrictEqual({ code, stdout, stderr }, {
      code: 42,
      stdout: 'the exit listener received code: 42\n',
      stderr: '',
    });
  });

  it('should throw for a rejected TLA promise and ignore explicit exit code via stdin', async () => {
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      '--no-warnings',
      fixtures.path('es-modules/tla/rejected-withexitcode.mjs'),
    ]);

    assert.match(stderr, /Error: Xyz/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
  });

  it('should exit successfully when calling `process.exit()` in `.mjs` file', async () => {
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      '--no-warnings',
      fixtures.path('es-modules/tla/process-exit.mjs'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 0);
  });

  it('should be unaffected by `process.exit()` in worker thread', async () => {
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      fixtures.path('es-modules/tla/unresolved-with-worker-process-exit.mjs'),
    ]);

    assert.match(stderr, /Warning: Detected unsettled top-level await at.+with-worker-process-exit\.mjs:5/);
    assert.match(stderr, /await new Promise/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 13);
  });

  it('should be unaffected by `process.exit()` in worker thread without warning', async () => {
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      '--no-warnings',
      fixtures.path('es-modules/tla/unresolved-with-worker-process-exit.mjs'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 13);
  });

  describe('with exit listener', () => {
    it('the process exit event should provide the correct code', async () => {
      const { code, stderr, stdout } = await spawnPromisified(execPath, [
        fixtures.path('es-modules/tla/unresolved-with-listener.mjs'),
      ]);

      assert.match(stderr, /Warning: Detected unsettled top-level await at/);
      assert.strictEqual(stdout,
                         'the exit listener received code: 13\n' +
                         'process.exitCode inside the exist listener: 13\n'
      );
      assert.strictEqual(code, 13);
    });

    it('should exit for an unsettled TLA promise and respect explicit exit code in process exit event', async () => {
      const { code, stderr, stdout } = await spawnPromisified(execPath, [
        '--no-warnings',
        fixtures.path('es-modules/tla/unresolved-withexitcode-and-listener.mjs'),
      ]);

      assert.deepStrictEqual({ code, stdout, stderr }, {
        code: 42,
        stdout: 'the exit listener received code: 42\n' +
                'process.exitCode inside the exist listener: 42\n',
        stderr: '',
      });
    });
  });
});
