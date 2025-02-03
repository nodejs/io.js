// Flags: --test-name-pattern=enabled
'use strict';
const common = require('../../../common');
const { suite, test } = require('node:test');

suite('async suite', async () => {
  await 1;
  test('enabled 1', common.mustCall());
  await 1;
  test('not run', common.mustNotCall());
  await 1;
});

suite('sync suite', () => {
  test('enabled 2', common.mustCall());
});
