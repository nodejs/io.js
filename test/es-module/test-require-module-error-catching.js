// This tests synchronous errors in ESM from require(esm) can be caught.

'use strict';

require('../common');
const assert = require('assert');

// Runtime errors from throw should be caught.
assert.throws(() => {
  require('../fixtures/es-modules/runtime-error-esm.js');
}, {
  message: 'hello'
});

// References errors should be caught too.
assert.throws(() => {
  require('../fixtures/es-modules/reference-error-esm.js');
}, {
  name: 'ReferenceError',
  message: 'exports is not defined in ES module scope'
});
