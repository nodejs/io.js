// This tests that after failing to require an ESM that contains TLA,
// retrying with require() still throws, and produces consistent results.
'use strict';
require('../common');
const assert = require('assert');

assert.throws(() => {
  require('../fixtures/es-modules/tla/resolved.mjs');
}, {
  code: 'ERR_REQUIRE_ASYNC_MODULE'
});
assert.throws(() => {
  require('../fixtures/es-modules/tla/resolved.mjs');
}, {
  code: 'ERR_REQUIRE_ASYNC_MODULE'
});
