// Flags: --experimental-require-module
'use strict';

const common = require('../common');
const assert = require('assert');

(async () => {
  await import('../fixtures/es-modules/tla/resolved.mjs');
  assert.throws(() => {
    require('../fixtures/es-modules/tla/resolved.mjs');
  }, {
    code: 'ERR_REQUIRE_ASYNC_MODULE',
  });
})().then(common.mustCall());
