'use strict';

require('../common');

// Test ensures that the function receives the url argument.

const assert = require('node:assert');

assert.throws(() => {
  URL.revokeObjectURL();
}, {
  code: 'ERR_MISSING_ARGS',
  name: 'TypeError',
});
