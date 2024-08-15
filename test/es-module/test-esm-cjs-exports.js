'use strict';

const { spawnPromisified } = require('../common');
const fixtures = require('../common/fixtures.js');
const assert = require('node:assert');
const { execPath } = require('node:process');
const { describe, it } = require('node:test');


describe('ESM: importing CJS', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should support valid CJS exports', async () => {
    const validEntry = fixtures.path('/es-modules/cjs-exports.mjs');
    const { code, signal, stdout } = await spawnPromisified(execPath, [validEntry]);

    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
    assert.strictEqual(stdout, 'ok\n');
  });

  it('should error on invalid CJS exports', async () => {
    const invalidEntry = fixtures.path('/es-modules/cjs-exports-invalid.mjs');
    const { code, signal, stderr } = await spawnPromisified(execPath, [invalidEntry]);

    assert.match(stderr, /SyntaxError: The requested module '\.\/invalid-cjs\.js' does not provide an export named 'default'/);
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });
});
