// Flags: --no-warnings
'use strict';

const { expectWarning } = require('../common');
const assert = require('assert');
const { test } = require('node:test');

expectWarning(
  'DeprecationWarning',
  'assert.fail() with more than one argument is deprecated. ' +
    'Please use assert.strictEqual() instead or only pass a message.',
  'DEP0094'
);

test('Two args only, operator defaults to "!="', () => {
  assert.throws(() => {
    assert.fail('first', 'second');
  }, {
    code: 'ERR_ASSERTION',
    name: 'AssertionError',
    message: '\'first\' != \'second\'',
    operator: '!=',
    actual: 'first',
    expected: 'second',
    generatedMessage: true
  });
});

test('Three args', () => {
  assert.throws(() => {
    assert.fail('ignored', 'ignored', 'another custom message');
  }, {
    code: 'ERR_ASSERTION',
    name: 'AssertionError',
    message: 'another custom message',
    operator: 'fail',
    actual: 'ignored',
    expected: 'ignored',
    generatedMessage: false
  });
});

test('Three args with custom Error', () => {
  assert.throws(() => {
    assert.fail(typeof 1, 'object', new TypeError('another custom message'));
  }, {
    name: 'TypeError',
    message: 'another custom message'
  });
});

test('No third arg (but a fourth arg)', () => {
  assert.throws(() => {
    assert.fail('first', 'second', undefined, 'operator');
  }, {
    code: 'ERR_ASSERTION',
    name: 'AssertionError',
    message: '\'first\' operator \'second\'',
    operator: 'operator',
    actual: 'first',
    expected: 'second'
  });
});

test('The stackFrameFunction should exclude the foo frame', () => {
  assert.throws(
    function foo() { assert.fail('first', 'second', 'message', '!==', foo); },
    (err) => !/^\s*at\sfoo\b/m.test(err.stack)
  );
});
