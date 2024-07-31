'use strict';
require('../common');
const assert = require('assert');
const { describe, it } = require('node:test');


describe('assert.CallTracker.getCalls()', { concurrency: !process.env.TEST_PARALLEL }, () => {
  const tracker = new assert.CallTracker();

  it('should return empty list when no calls', () => {
    const fn = tracker.calls();
    assert.deepStrictEqual(tracker.getCalls(fn), []);
  });

  it('should return calls', () => {
    const fn = tracker.calls(() => {});
    const arg1 = {};
    const arg2 = {};
    fn(arg1, arg2);
    fn.call(arg2, arg2);
    assert.deepStrictEqual(tracker.getCalls(fn), [
      { arguments: [arg1, arg2], thisArg: undefined },
      { arguments: [arg2], thisArg: arg2 }]);
  });

  it('should throw when getting calls of a non-tracked function', () => {
    [() => {}, 1, true, null, undefined, {}, []].forEach((fn) => {
      assert.throws(() => tracker.getCalls(fn), { code: 'ERR_INVALID_ARG_VALUE' });
    });
  });

  it('should return a frozen object', () => {
    const fn = tracker.calls();
    fn();
    const calls = tracker.getCalls(fn);
    assert.throws(() => calls.push(1), /object is not extensible/);
    assert.throws(() => Object.assign(calls[0], { foo: 'bar' }), /object is not extensible/);
    assert.throws(() => calls[0].arguments.push(1), /object is not extensible/);
  });
});

describe('assert.CallTracker.reset()', () => {
  const tracker = new assert.CallTracker();

  it('should reset calls', () => {
    const fn = tracker.calls();
    fn();
    fn();
    fn();
    assert.strictEqual(tracker.getCalls(fn).length, 3);
    tracker.reset(fn);
    assert.deepStrictEqual(tracker.getCalls(fn), []);
  });

  it('should reset all calls', () => {
    const fn1 = tracker.calls();
    const fn2 = tracker.calls();
    fn1();
    fn2();
    assert.strictEqual(tracker.getCalls(fn1).length, 1);
    assert.strictEqual(tracker.getCalls(fn2).length, 1);
    tracker.reset();
    assert.deepStrictEqual(tracker.getCalls(fn1), []);
    assert.deepStrictEqual(tracker.getCalls(fn2), []);
  });


  it('should throw when resetting a non-tracked function', () => {
    [() => {}, 1, true, null, {}, []].forEach((fn) => {
      assert.throws(() => tracker.reset(fn), { code: 'ERR_INVALID_ARG_VALUE' });
    });
  });
});
