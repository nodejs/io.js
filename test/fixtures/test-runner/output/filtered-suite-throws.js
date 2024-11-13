// Flags: --test-name-pattern=enabled
'use strict';
const common = require('../../../common');
const { suite, test } = require('node:test');

suite('suite 1', () => {
  throw new Error('boom 1');
  test('enabled - should not run', common.mustNotCall());
});

suite('suite 2', () => {
  test('enabled - should get cancelled', common.mustNotCall());
  throw new Error('boom 1');
});
