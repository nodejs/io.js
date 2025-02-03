'use strict';

const { spawnPromisified } = require('../common');
const fixtures = require('../common/fixtures.js');
const assert = require('node:assert');
const path = require('node:path');
const { execPath } = require('node:process');
const { describe, it } = require('node:test');


describe('ESM: Package.json', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should throw on invalid pson', async () => {
    const entry = fixtures.path('/es-modules/import-invalid-pjson.mjs');
    const invalidJson = fixtures.path('/node_modules/invalid-pjson/package.json');

    const { code, signal, stderr } = await spawnPromisified(execPath, [entry]);

    assert.ok(stderr.includes('code: \'ERR_INVALID_PACKAGE_CONFIG\''), stderr);
    assert.ok(
      stderr.includes(
        `Invalid package config ${path.toNamespacedPath(invalidJson)} while importing "invalid-pjson" from ${entry}.`
      ) || stderr.includes(
        `Invalid package config ${path.toNamespacedPath(invalidJson)} while importing "invalid-pjson" from ${path.toNamespacedPath(entry)}.`
      ),
      stderr
    );
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });
});
