// Flags: --test-skip-pattern=disabled --test-name-pattern=enabled
'use strict';
const common = require('../../../common');
const {
  test,
} = require('node:test');

test('disabled', common.mustNotCall());
test('enabled', common.mustCall());
test('enabled disabled', common.mustNotCall());
