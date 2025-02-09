'use strict';

const common = require('../common');
const assert = require('node:assert');
const { test } = require('node:test');

if (!process.config.variables.node_without_node_options) {
  common.skip('Requires the lack of NODE_OPTIONS support');
}

const relativePath = '../fixtures/dotenv/node-options.env';

test('.env does not support NODE_OPTIONS', async () => {
  const code = 'assert.strictEqual(process.permission, undefined)';
  const child = await common.spawnPromisified(
    process.execPath,
    [ `--env-file=${relativePath}`, '--eval', code ],
    { cwd: __dirname },
  );
  // NODE_NO_WARNINGS is set, so `stderr` should not contain
  // "ExperimentalWarning: Permission is an experimental feature" message
  // and thus be empty
  assert.strictEqual(child.stdout, '');
  assert.strictEqual(child.stderr, '');
  assert.strictEqual(child.code, 0);
});
