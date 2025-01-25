'use strict';

const common = require('../common');
const path = require('path');
const { test } = require('node:test');
const assert = require('node:assert');

test('Running from a directory with non-ASCII characters', async () => {
  const nonAsciiPath = path.resolve(__dirname, '../fixtures/全角文字/index.js');
  const { stdout } = await common.spawnPromisified(process.execPath, [nonAsciiPath]);
  assert.strictEqual(stdout, 'check non-ascii\n');
});
