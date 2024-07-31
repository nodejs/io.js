'use strict';
// Flags: --expose-internals
require('../common');
const assert = require('assert');
const { internalBinding } = require('internal/test/binding');

assert.throws(() => internalBinding('internal_only_v8'), {
  code: 'ERR_INVALID_MODULE'
});
