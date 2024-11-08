// Flags: --no-experimental-require-module
// Previously, this tested that require(esm) throws ERR_REQUIRE_ESM, which is no longer applicable
// since require(esm) is now supported. The test has been repurposed to ensure that the old behavior
// is preserved when the --no-experimental-require-module flag is used.

'use strict';
require('../common');
const assert = require('assert');

assert.throws(
  () => require('../fixtures/es-modules/test-esm-ok.mjs'),
  {
    message: /dynamic import\(\) which is available in all CommonJS modules/,
    code: 'ERR_REQUIRE_ESM'
  }
);
