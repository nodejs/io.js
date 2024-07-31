// Flags: --expose-internals
'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');

const {
  assertIsObject,
  assertWithinRange,
  sessionName,
  assertIsArray
} = require('internal/http2/util');

// Code coverage for sessionName utility function
assert.strictEqual(sessionName(0), 'server');
assert.strictEqual(sessionName(1), 'client');
[2, '', 'test', {}, [], true].forEach((i) => {
  assert.strictEqual(sessionName(2), '<invalid>');
});

// Code coverage for assertWithinRange function
assert.throws(
  () => assertWithinRange('test', -1),
  {
    code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
    name: 'RangeError',
    message: 'Invalid value for setting "test": -1'
  });

assertWithinRange('test', 1);

assert.throws(
  () => assertIsObject('foo', 'test'),
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "test" argument must be of type object. Received ' +
             "type string ('foo')"
  });

assert.throws(
  () => assertIsObject('foo', 'test', ['Date']),
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "test" argument must be an instance of Date. Received type ' +
             "string ('foo')"
  });

assertIsObject({}, 'test');

assert.throws(
  () => assertIsArray('foo', 'test'), {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "test" argument must be an instance of Array. Received type ' +
    "string ('foo')"
  }
);

assert.throws(
  () => assertIsArray({}, 'test'), {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "test" argument must be an instance of Array. Received an instance of Object'
  }
);

assert.throws(
  () => assertIsArray(1, 'test'), {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "test" argument must be an instance of Array. Received type ' +
    'number (1)'
  }
);

assertIsArray([], 'test');
