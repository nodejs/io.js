'use strict';

// Tests below are not from WPT.

require('../common');
const assert = require('assert');

assert.deepStrictEqual(
  Object.getOwnPropertyDescriptor(globalThis, 'URL'),
  {
    value: URL,
    writable: true,
    configurable: true,
    enumerable: false
  }
);

assert.deepStrictEqual(
  Object.getOwnPropertyDescriptor(globalThis, 'URLSearchParams'),
  {
    value: URLSearchParams,
    writable: true,
    configurable: true,
    enumerable: false
  }
);
