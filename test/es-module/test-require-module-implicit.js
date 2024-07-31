// Flags: --experimental-require-module
'use strict';

// Tests that require()ing modules without explicit module type information
// warns and errors.
const common = require('../common');
const assert = require('assert');

{
  // .mjs should not be matched as default extensions.
  const id = '../fixtures/es-modules/should-not-be-resolved';
  assert.throws(() => {
    require(id);
  }, {
    code: 'MODULE_NOT_FOUND'
  });
  const mod = require(`${id}.mjs`);
  common.expectRequiredModule(mod, { hello: 'world' });
}
