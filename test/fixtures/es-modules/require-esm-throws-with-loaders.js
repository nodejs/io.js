'use strict';
const { readFile, __fromLoader } = require('fs');
const assert = require('assert');

assert.throws(() => require('./test-esm-ok.mjs'), { code: 'ERR_REQUIRE_ESM' });

assert(readFile);
assert(__fromLoader);
