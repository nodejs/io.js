// Flags: --expose-internals --no-warnings
'use strict';

const common = require('../common');
const assert = require('assert');
const { on, EventEmitter } = require('events');
const {
  NodeEventTarget,
  kEvents
} = require('internal/event_target');

async function basic() {
  const ee = new EventEmitter();
  process.nextTick(() => {
    ee.emit('foo', 'bar');
    // 'bar' is a spurious event, we are testing
    // that it does not show up in the iterable
    ee.emit('bar', 24);
    ee.emit('foo', 42);
  });

  const iterable = on(ee, 'foo');

  const expected = [['bar'], [42]];

  for await (const event of iterable) {
    const current = expected.shift();

    assert.deepStrictEqual(current, event);

    if (expected.length === 0) {
      break;
    }
  }
  assert.strictEqual(ee.listenerCount('foo'), 0);
  assert.strictEqual(ee.listenerCount('error'), 0);
}

async function invalidArgType() {
  assert.throws(() => on({}, 'foo'), common.expectsError({
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
  }));

  const ee = new EventEmitter();

  [1, 'hi', null, false, () => {}, Symbol(), 1n].map((options) => {
    return assert.throws(() => on(ee, 'foo', options), common.expectsError({
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
    }));
  });
}

async function error() {
  const ee = new EventEmitter();
  const _err = new Error('kaboom');
  process.nextTick(() => {
    ee.emit('error', _err);
  });

  const iterable = on(ee, 'foo');
  let looped = false;
  let thrown = false;

  try {
    // eslint-disable-next-line no-unused-vars
    for await (const event of iterable) {
      looped = true;
    }
  } catch (err) {
    thrown = true;
    assert.strictEqual(err, _err);
  }
  assert.strictEqual(thrown, true);
  assert.strictEqual(looped, false);
}

async function errorDelayed() {
  const ee = new EventEmitter();
  const _err = new Error('kaboom');
  process.nextTick(() => {
    ee.emit('foo', 42);
    ee.emit('error', _err);
  });

  const iterable = on(ee, 'foo');
  const expected = [[42]];
  let thrown = false;

  try {
    for await (const event of iterable) {
      const current = expected.shift();
      assert.deepStrictEqual(current, event);
    }
  } catch (err) {
    thrown = true;
    assert.strictEqual(err, _err);
  }
  assert.strictEqual(thrown, true);
  assert.strictEqual(ee.listenerCount('foo'), 0);
  assert.strictEqual(ee.listenerCount('error'), 0);
}

async function throwInLoop() {
  const ee = new EventEmitter();
  const _err = new Error('kaboom');

  process.nextTick(() => {
    ee.emit('foo', 42);
  });

  try {
    for await (const event of on(ee, 'foo')) {
      assert.deepStrictEqual(event, [42]);
      throw _err;
    }
  } catch (err) {
    assert.strictEqual(err, _err);
  }

  assert.strictEqual(ee.listenerCount('foo'), 0);
  assert.strictEqual(ee.listenerCount('error'), 0);
}

async function next() {
  const ee = new EventEmitter();
  const iterable = on(ee, 'foo');

  process.nextTick(function() {
    ee.emit('foo', 'bar');
    ee.emit('foo', 42);
    iterable.return();
  });

  const results = await Promise.all([
    iterable.next(),
    iterable.next(),
    iterable.next(),
  ]);

  assert.deepStrictEqual(results, [{
    value: ['bar'],
    done: false,
  }, {
    value: [42],
    done: false,
  }, {
    value: undefined,
    done: true,
  }]);

  assert.deepStrictEqual(await iterable.next(), {
    value: undefined,
    done: true,
  });
}

async function nextError() {
  const ee = new EventEmitter();
  const iterable = on(ee, 'foo');
  const _err = new Error('kaboom');
  process.nextTick(function() {
    ee.emit('error', _err);
  });
  const results = await Promise.allSettled([
    iterable.next(),
    iterable.next(),
    iterable.next(),
  ]);
  assert.deepStrictEqual(results, [{
    status: 'rejected',
    reason: _err,
  }, {
    status: 'fulfilled',
    value: {
      value: undefined,
      done: true,
    },
  }, {
    status: 'fulfilled',
    value: {
      value: undefined,
      done: true,
    },
  }]);
  assert.strictEqual(ee.listeners('error').length, 0);
}

async function iterableThrow() {
  const ee = new EventEmitter();
  const iterable = on(ee, 'foo');

  process.nextTick(() => {
    ee.emit('foo', 'bar');
    ee.emit('foo', 42); // lost in the queue
    iterable.throw(_err);
  });

  const _err = new Error('kaboom');
  let thrown = false;

  assert.throws(() => {
    // No argument
    iterable.throw();
  }, {
    message: 'The "EventEmitter.AsyncIterator" property must be' +
    ' an instance of Error. Received undefined',
    name: 'TypeError',
  });

  const expected = [['bar'], [42]];

  try {
    for await (const event of iterable) {
      assert.deepStrictEqual(event, expected.shift());
    }
  } catch (err) {
    thrown = true;
    assert.strictEqual(err, _err);
  }
  assert.strictEqual(thrown, true);
  assert.strictEqual(expected.length, 0);
  assert.strictEqual(ee.listenerCount('foo'), 0);
  assert.strictEqual(ee.listenerCount('error'), 0);
}

async function eventTarget() {
  const et = new EventTarget();
  const tick = () => et.dispatchEvent(new Event('tick'));
  const interval = setInterval(tick, 0);
  let count = 0;
  for await (const [ event ] of on(et, 'tick')) {
    count++;
    assert.strictEqual(event.type, 'tick');
    if (count >= 5) {
      break;
    }
  }
  assert.strictEqual(count, 5);
  clearInterval(interval);
}

async function errorListenerCount() {
  const et = new EventEmitter();
  on(et, 'foo');
  assert.strictEqual(et.listenerCount('error'), 1);
}

async function nodeEventTarget() {
  const et = new NodeEventTarget();
  const tick = () => et.dispatchEvent(new Event('tick'));
  const interval = setInterval(tick, 0);
  let count = 0;
  for await (const [ event] of on(et, 'tick')) {
    count++;
    assert.strictEqual(event.type, 'tick');
    if (count >= 5) {
      break;
    }
  }
  assert.strictEqual(count, 5);
  clearInterval(interval);
}

async function abortableOnBefore() {
  const ee = new EventEmitter();
  const abortedSignal = AbortSignal.abort();
  [1, {}, null, false, 'hi'].forEach((signal) => {
    assert.throws(() => on(ee, 'foo', { signal }), {
      code: 'ERR_INVALID_ARG_TYPE',
    });
  });
  assert.throws(() => on(ee, 'foo', { signal: abortedSignal }), {
    name: 'AbortError',
  });
}

async function eventTargetAbortableOnBefore() {
  const et = new EventTarget();
  const abortedSignal = AbortSignal.abort();
  [1, {}, null, false, 'hi'].forEach((signal) => {
    assert.throws(() => on(et, 'foo', { signal }), {
      code: 'ERR_INVALID_ARG_TYPE',
    });
  });
  assert.throws(() => on(et, 'foo', { signal: abortedSignal }), {
    name: 'AbortError',
  });
}

async function abortableOnAfter() {
  const ee = new EventEmitter();
  const ac = new AbortController();

  const i = setInterval(() => ee.emit('foo', 'foo'), 10);

  async function foo() {
    for await (const f of on(ee, 'foo', { signal: ac.signal })) {
      assert.strictEqual(f, 'foo');
    }
  }

  foo().catch(common.mustCall((error) => {
    assert.strictEqual(error.name, 'AbortError');
  })).finally(() => {
    clearInterval(i);
  });

  process.nextTick(() => ac.abort());
}

async function eventTargetAbortableOnAfter() {
  const et = new EventTarget();
  const ac = new AbortController();

  const i = setInterval(() => et.dispatchEvent(new Event('foo')), 10);

  async function foo() {
    for await (const f of on(et, 'foo', { signal: ac.signal })) {
      assert(f);
    }
  }

  foo().catch(common.mustCall((error) => {
    assert.strictEqual(error.name, 'AbortError');
  })).finally(() => {
    clearInterval(i);
  });

  process.nextTick(() => ac.abort());
}

async function eventTargetAbortableOnAfter2() {
  const et = new EventTarget();
  const ac = new AbortController();

  const i = setInterval(() => et.dispatchEvent(new Event('foo')), 10);

  async function foo() {
    for await (const f of on(et, 'foo', { signal: ac.signal })) {
      assert(f);
      // Cancel after a single event has been triggered.
      ac.abort();
    }
  }

  foo().catch(common.mustCall((error) => {
    assert.strictEqual(error.name, 'AbortError');
  })).finally(() => {
    clearInterval(i);
  });
}

async function abortableOnAfterDone() {
  const ee = new EventEmitter();
  const ac = new AbortController();

  const i = setInterval(() => ee.emit('foo', 'foo'), 1);
  let count = 0;

  async function foo() {
    for await (const f of on(ee, 'foo', { signal: ac.signal })) {
      assert.strictEqual(f[0], 'foo');
      if (++count === 5)
        break;
    }
    ac.abort();  // No error will occur
  }

  foo().finally(() => {
    clearInterval(i);
  });
}

async function abortListenerRemovedAfterComplete() {
  const ee = new EventEmitter();
  const ac = new AbortController();

  const i = setInterval(() => ee.emit('foo', 'foo'), 1);
  try {
    // Below: either the kEvents map is empty or the 'abort' listener list is empty

    // Return case
    const endedIterator = on(ee, 'foo', { signal: ac.signal });
    assert.ok(ac.signal[kEvents].get('abort').size > 0);
    endedIterator.return();
    assert.strictEqual(ac.signal[kEvents].get('abort')?.size ?? ac.signal[kEvents].size, 0);

    // Throw case
    const throwIterator = on(ee, 'foo', { signal: ac.signal });
    assert.ok(ac.signal[kEvents].get('abort').size > 0);
    throwIterator.throw(new Error());
    assert.strictEqual(ac.signal[kEvents].get('abort')?.size ?? ac.signal[kEvents].size, 0);

    // Abort case
    on(ee, 'foo', { signal: ac.signal });
    assert.ok(ac.signal[kEvents].get('abort').size > 0);
    ac.abort(new Error());
    assert.strictEqual(ac.signal[kEvents].get('abort')?.size ?? ac.signal[kEvents].size, 0);
  } finally {
    clearInterval(i);
  }
}

async function run() {
  const funcs = [
    basic,
    invalidArgType,
    error,
    errorDelayed,
    throwInLoop,
    next,
    nextError,
    iterableThrow,
    eventTarget,
    errorListenerCount,
    nodeEventTarget,
    abortableOnBefore,
    abortableOnAfter,
    eventTargetAbortableOnBefore,
    eventTargetAbortableOnAfter,
    eventTargetAbortableOnAfter2,
    abortableOnAfterDone,
    abortListenerRemovedAfterComplete,
  ];

  for (const fn of funcs) {
    await fn();
  }
}

run().then(common.mustCall());
