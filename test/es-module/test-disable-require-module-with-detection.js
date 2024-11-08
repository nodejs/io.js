// Flags: --no-experimental-require-module
'use strict';

// Tests that --experimental-require-module is not implied by --experimental-detect-module
// and is checked independently.
require('../common');
const assert = require('assert');

// Check that require() still throws SyntaxError for an ambiguous module that's detected to be ESM.
// TODO(joyeecheung): now that --experimental-detect-module is unflagged, it makes more sense
// to either throw ERR_REQUIRE_ESM for require() of detected ESM instead, or add a hint about the
// use of require(esm) to the SyntaxError.

assert.throws(
  () => require('../fixtures/es-modules/loose.js'),
  {
    name: 'SyntaxError',
    message: /Unexpected token 'export'/
  });
