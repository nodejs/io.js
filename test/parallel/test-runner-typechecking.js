'use strict';
require('../common');

// Return type of shorthands should be consistent
// with the return type of test

const assert = require('assert');
const { test, describe, it } = require('node:test');

const testOnly = test('only test', { only: true });
const testTodo = test('todo test', { todo: true });
const testSkip = test('skip test', { skip: true });
const testOnlyShorthand = test.only('only test shorthand');
const testTodoShorthand = test.todo('todo test shorthand');
const testSkipShorthand = test.skip('skip test shorthand');

describe('\'node:test\' and its shorthands should return the same', () => {
  it('should return undefined', () => {
    assert.strictEqual(testOnly, undefined);
    assert.strictEqual(testTodo, undefined);
    assert.strictEqual(testSkip, undefined);
    assert.strictEqual(testOnlyShorthand, undefined);
    assert.strictEqual(testTodoShorthand, undefined);
    assert.strictEqual(testSkipShorthand, undefined);
  });
});
