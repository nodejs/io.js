'use strict';
require('../common');
const assert = require('node:assert');
const { test, assert: testAssertions } = require('node:test');

testAssertions.register('isOdd', (n) => {
  assert.strictEqual(n % 2, 1);
});

testAssertions.register('ok', () => {
  return 'ok';
});

testAssertions.register('snapshot', () => {
  return 'snapshot';
});

testAssertions.register('deepStrictEqual', () => {
  return 'deepStrictEqual';
});

testAssertions.register('context', function() {
  return this;
});

test('throws if name is not a string', () => {
  assert.throws(() => {
    testAssertions.register(5);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "name" argument must be of type string. Received type number (5)'
  });
});

test('throws if fn is not a function', () => {
  assert.throws(() => {
    testAssertions.register('foo', 5);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "fn" argument must be of type function. Received type number (5)'
  });
});

test('invokes a custom assertion as part of the test plan', (t) => {
  t.plan(2);
  t.assert.isOdd(5);
  assert.throws(() => {
    t.assert.isOdd(4);
  }, {
    code: 'ERR_ASSERTION',
    message: /Expected values to be strictly equal/
  });
});

test('can override existing assertions', (t) => {
  assert.strictEqual(t.assert.ok(), 'ok');
  assert.strictEqual(t.assert.snapshot(), 'snapshot');
  assert.strictEqual(t.assert.deepStrictEqual(), 'deepStrictEqual');
});

test('"this" is set to the TestContext', (t) => {
  assert.strictEqual(t.assert.context(), t);
});
