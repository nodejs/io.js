// Flags: --no-experimental-require-module
// Previously, this tested that require(esm) throws ERR_REQUIRE_ESM, which is no longer applicable
// since require(esm) is now supported. The test has been repurposed to ensure that the old behavior
// is preserved when the --no-experimental-require-module flag is used.

'use strict';
require('../common');
const assert = require('assert');
const { describe, it } = require('node:test');

describe('Errors related to ESM type field', () => {
  it('Should throw an error when loading CJS from a `type: "module"` package.', () => {
    assert.throws(() => require('../fixtures/es-modules/package-type-module/index.js'), {
      code: 'ERR_REQUIRE_ESM'
    });
  });
});
