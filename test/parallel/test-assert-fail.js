'use strict';

require('../common');
const assert = require('assert');
const { test } = require('node:test');

test('No args', () => {
  assert.throws(
    () => { assert.fail(); },
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: 'Failed',
      operator: 'fail',
      actual: undefined,
      expected: undefined,
      generatedMessage: true,
      stack: /Failed/
    }
  );
});

test('One arg = message', () => {
  assert.throws(() => {
    assert.fail('custom message');
  }, {
    code: 'ERR_ASSERTION',
    name: 'AssertionError',
    message: 'custom message',
    operator: 'fail',
    actual: undefined,
    expected: undefined,
    generatedMessage: false
  });
});

test('One arg = Error', () => {
  assert.throws(() => {
    assert.fail(new TypeError('custom message'));
  }, {
    name: 'TypeError',
    message: 'custom message'
  });
});

test('Object prototype get', () => {
  Object.prototype.get = () => { throw new Error('failed'); };
  assert.throws(() => assert.fail(''), { code: 'ERR_ASSERTION' });
  delete Object.prototype.get;
});
