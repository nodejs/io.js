'use strict';

const { skip, spawnPromisified } = require('../common');
const fixtures = require('../common/fixtures');
const { strictEqual } = require('node:assert');
const { test } = require('node:test');

if (!process.config.variables.node_use_amaro) skip('Requires Amaro');

test('util.stripTypescriptTypes', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    fixtures.path('test-util-stripTypescriptTypes.js'),
  ]);

  strictEqual(result.stdout, '');
  strictEqual(result.stderr, '');
  strictEqual(result.code, 0);
});
