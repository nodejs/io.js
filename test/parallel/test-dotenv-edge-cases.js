'use strict';

const common = require('../common');
const assert = require('node:assert');
const path = require('node:path');
const { describe, it } = require('node:test');
const fixtures = require('../common/fixtures');

const validEnvFilePath = '../fixtures/dotenv/valid.env';
const nodeOptionsEnvFilePath = '../fixtures/dotenv/node-options.env';

describe('.env supports edge cases', () => {

  it('supports multiple declarations', async () => {
    // process.env.BASIC is equal to `basic` because the second .env file overrides it.
    const code = `
      const assert = require('assert');
      assert.strictEqual(process.env.BASIC, 'basic');
      assert.strictEqual(process.env.NODE_NO_WARNINGS, '1');
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ `--env-file=${nodeOptionsEnvFilePath}`, `--env-file=${validEnvFilePath}`, '--eval', code ],
      { cwd: __dirname },
    );
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
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

  it('should handle non-existent .env file', async () => {
    const code = `
      require('assert').strictEqual(1, 1)
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--env-file=.env', '--eval', code ],
      { cwd: __dirname },
    );
    assert.notStrictEqual(child.stderr.toString(), '');
    assert.strictEqual(child.code, 9);
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
});
