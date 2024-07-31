// Flags: --test-only
'use strict';
const common = require('../common');
const { test } = require('node:test');
const { defaultMaxListeners } = require('node:events');

process.on('warning', common.mustNotCall());

for (let i = 0; i < defaultMaxListeners + 1; ++i) {
  test(`test ${i + 1}`);
}
