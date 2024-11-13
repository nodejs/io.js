'use strict';

require('../common');
const assert = require('node:assert');

assert.throws(
  () => ReadableStream.from({}),
  { code: 'ERR_ARG_NOT_ITERABLE', name: 'TypeError' },
);
