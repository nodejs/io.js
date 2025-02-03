'use strict';

// This tests module.enableCompileCache() throws when an invalid argument is passed.

require('../common');
const { enableCompileCache } = require('module');
const assert = require('assert');

for (const invalid of [0, null, false, () => {}, {}, []]) {
  assert.throws(() => enableCompileCache(invalid), { code: 'ERR_INVALID_ARG_TYPE' });
}
