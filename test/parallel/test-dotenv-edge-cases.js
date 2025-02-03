'use strict';

const common = require('../common');
const assert = require('node:assert');
const path = require('node:path');
const { describe, it } = require('node:test');
const fixtures = require('../common/fixtures');

const validEnvFilePath = '../fixtures/dotenv/valid.env';
const nodeOptionsEnvFilePath = '../fixtures/dotenv/node-options.env';
const noFinalNewlineEnvFilePath = '../fixtures/dotenv/no-final-newline.env';
const noFinalNewlineSingleQuotesEnvFilePath = '../fixtures/dotenv/no-final-newline-single-quotes.env';

describe('.env supports edge cases', () => {
  it('supports multiple declarations, including optional ones', async () => {
    const code = `
      const assert = require('assert');
      assert.strictEqual(process.env.BASIC, 'basic');
      assert.strictEqual(process.env.NODE_NO_WARNINGS, '1');
    `.trim();
    const children = await Promise.all(Array.from({ length: 4 }, (_, i) =>
      common.spawnPromisified(
        process.execPath,
        [
          // Bitwise AND to create all 4 possible combinations:
          // i & 0b01 is truthy when i has value 0bx1 (i.e. 0b01 (1) and 0b11 (3)), falsy otherwise.
          // i & 0b10 is truthy when i has value 0b1x (i.e. 0b10 (2) and 0b11 (3)), falsy otherwise.
          `${i & 0b01 ? '--env-file' : '--env-file-if-exists'}=${nodeOptionsEnvFilePath}`,
          `${i & 0b10 ? '--env-file' : '--env-file-if-exists'}=${validEnvFilePath}`,
          '--eval', code,
        ],
        { cwd: __dirname },
      )));
    assert.deepStrictEqual(children, Array.from({ length: 4 }, () => ({
      code: 0,
      signal: null,
      stdout: '',
      stderr: '',
    })));
  });

  it('supports absolute paths', async () => {
    const code = `
      require('assert').strictEqual(process.env.BASIC, 'basic');
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ `--env-file=${path.resolve(__dirname, validEnvFilePath)}`, '--eval', code ],
    );
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('supports a space instead of \'=\' for the flag ', async () => {
    const code = `
      require('assert').strictEqual(process.env.BASIC, 'basic');
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--env-file', validEnvFilePath, '--eval', code ],
      { cwd: __dirname },
    );
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('should handle non-existent .env file', async () => {
    const code = `
      require('assert').strictEqual(1, 1)
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--env-file=.env', '--eval', code ],
      { cwd: __dirname },
    );
    assert.notStrictEqual(child.stderr, '');
    assert.strictEqual(child.code, 9);
  });

  it('should handle non-existent optional .env file', async () => {
    const code = `
      require('assert').strictEqual(1,1);
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      ['--env-file-if-exists=.env', '--eval', code],
      { cwd: __dirname },
    );
    assert.notStrictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('should not override existing environment variables but introduce new vars', async () => {
    const code = `
      require('assert').strictEqual(process.env.BASIC, 'existing');
      require('assert').strictEqual(process.env.AFTER_LINE, 'after_line');
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ `--env-file=${validEnvFilePath}`, '--eval', code ],
      { cwd: __dirname, env: { ...process.env, BASIC: 'existing' } },
    );
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('should handle multiline quoted values', async () => {
    // Ref: https://github.com/nodejs/node/issues/52248
    const code = `
      process.loadEnvFile('./multiline.env');
      require('node:assert').ok(process.env.JWT_PUBLIC_KEY);
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--eval', code ],
      { cwd: fixtures.path('dotenv') },
    );
    assert.strictEqual(child.stdout, '');
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('should handle empty value without a newline at the EOF', async () => {
    // Ref: https://github.com/nodejs/node/issues/52466
    const code = `
      process.loadEnvFile('./eof-without-value.env');
      require('assert').strictEqual(process.env.BASIC, 'value');
      require('assert').strictEqual(process.env.EMPTY, '');
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--eval', code ],
      { cwd: fixtures.path('dotenv') },
    );
    assert.strictEqual(child.stdout, '');
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('should handle when --env-file is passed along with --', async () => {
    const child = await common.spawnPromisified(
      process.execPath,
      [
        '--eval', `require('assert').strictEqual(process.env.BASIC, undefined);`,
        '--', '--env-file', validEnvFilePath,
      ],
      { cwd: __dirname },
    );
    assert.strictEqual(child.stdout, '');
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('should handle file without a final newline', async () => {
    const code = `
      require('assert').strictEqual(process.env.BASIC, 'basic');
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ `--env-file=${path.resolve(__dirname, noFinalNewlineEnvFilePath)}`, '--eval', code ],
    );

    const SingleQuotesChild = await common.spawnPromisified(
      process.execPath,
      [ `--env-file=${path.resolve(__dirname, noFinalNewlineSingleQuotesEnvFilePath)}`, '--eval', code ],
    );

    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
    assert.strictEqual(SingleQuotesChild.stderr, '');
    assert.strictEqual(SingleQuotesChild.code, 0);
  });

  it('should reject invalid env file flag', async () => {
    const child = await common.spawnPromisified(
      process.execPath,
      ['--env-file-ABCD', validEnvFilePath],
      { cwd: __dirname },
    );

    assert.strictEqual(child.stdout, '');
    assert.strictEqual(child.code, 9);
    assert.match(child.stderr, /bad option: --env-file-ABCD/);
  });
});
