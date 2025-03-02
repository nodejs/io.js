// Flags: --no-warnings --expose-internals
'use strict';
const common = require('../common');
const assert = require('assert');
const timers = require('timers');
const { promisify } = require('util');

const { getEventListeners } = require('events');
const { NodeEventTarget } = require('internal/event_target');

const timerPromises = require('timers/promises');

const setPromiseTimeout = promisify(timers.setTimeout);

const { setInterval } = timerPromises;

process.on('multipleResolves', common.mustNotCall());

{
  const iterable = setInterval(1, undefined);
  const iterator = iterable[Symbol.asyncIterator]();
  const promise = iterator.next();
  promise.then(common.mustCall((result) => {
    assert.ok(!result.done, 'iterator was wrongly marked as done');
    assert.strictEqual(result.value, undefined);
    return iterator.return();
  })).then(common.mustCall());
}

{
  const iterable = setInterval(1, 'foobar');
  const iterator = iterable[Symbol.asyncIterator]();
  const promise = iterator.next();
  promise.then(common.mustCall((result) => {
    assert.ok(!result.done, 'iterator was wrongly marked as done');
    assert.strictEqual(result.value, 'foobar');
    return iterator.return();
  })).then(common.mustCall());
}

{
  const iterable = setInterval(1, 'foobar');
  const iterator = iterable[Symbol.asyncIterator]();
  const promise = iterator.next();
  promise
    .then(common.mustCall((result) => {
      assert.ok(!result.done, 'iterator was wrongly marked as done');
      assert.strictEqual(result.value, 'foobar');
      return iterator.next();
    }))
    .then(common.mustCall((result) => {
      assert.ok(!result.done, 'iterator was wrongly marked as done');
      assert.strictEqual(result.value, 'foobar');
      return iterator.return();
    }))
    .then(common.mustCall());
}

{
  const signal = AbortSignal.abort(); // Abort in advance

  const iterable = setInterval(1, undefined, { signal });
  const iterator = iterable[Symbol.asyncIterator]();
  assert.rejects(iterator.next(), /AbortError/).then(common.mustCall());
}

{
  const ac = new AbortController();
  const { signal } = ac;

  const iterable = setInterval(100, undefined, { signal });
  const iterator = iterable[Symbol.asyncIterator]();

  // This promise should take 100 seconds to resolve, so now aborting it should
  // mean we abort early
  const promise = iterator.next();

  ac.abort(); // Abort in after we have a next promise

  assert.rejects(promise, /AbortError/).then(common.mustCall());
}

{
  // Check aborting after getting a value.
  const ac = new AbortController();
  const { signal } = ac;

  const iterable = setInterval(100, undefined, { signal });
  const iterator = iterable[Symbol.asyncIterator]();

  const promise = iterator.next();
  const abortPromise = promise.then(common.mustCall(() => ac.abort()))
    .then(() => iterator.next());
  assert.rejects(abortPromise, /AbortError/).then(common.mustCall());
}

{
  [1, '', Infinity, null, {}].forEach((ref) => {
    const iterable = setInterval(10, undefined, { ref });
    assert.rejects(() => iterable[Symbol.asyncIterator]().next(), /ERR_INVALID_ARG_TYPE/)
      .then(common.mustCall());
  });

  [1, '', Infinity, null, {}].forEach((signal) => {
    const iterable = setInterval(10, undefined, { signal });
    assert.rejects(() => iterable[Symbol.asyncIterator]().next(), /ERR_INVALID_ARG_TYPE/)
      .then(common.mustCall());
  });

  [1, '', Infinity, null, true, false].forEach((options) => {
    const iterable = setInterval(10, undefined, options);
    assert.rejects(() => iterable[Symbol.asyncIterator]().next(), /ERR_INVALID_ARG_TYPE/)
      .then(common.mustCall());
  });
}

{
  // Check that timer adding signals does not leak handlers
  const signal = new NodeEventTarget();
  signal.aborted = false;
  const iterator = setInterval(1, undefined, { signal });
  iterator.next().then(common.mustCall(() => {
    assert.strictEqual(getEventListeners(signal, 'abort').length, 1);
    iterator.return();
  })).finally(common.mustCall(() => {
    assert.strictEqual(getEventListeners(signal, 'abort').length, 0);
  }));
}

{
  // Check that break removes the signal listener
  const signal = new NodeEventTarget();
  signal.aborted = false;
  async function tryBreak() {
    const iterator = setInterval(10, undefined, { signal });
    let i = 0;
    // eslint-disable-next-line no-unused-vars
    for await (const _ of iterator) {
      if (i === 0) {
        assert.strictEqual(getEventListeners(signal, 'abort').length, 1);
      }
      i++;
      if (i === 2) {
        break;
      }
    }
    assert.strictEqual(i, 2);
    assert.strictEqual(getEventListeners(signal, 'abort').length, 0);
  }

  tryBreak().then(common.mustCall());
}

{
  common.spawnPromisified(process.execPath, ['-pe', "const assert = require('assert');" +
    'const interval = require(\'timers/promises\')' +
    '.setInterval(1000, null, { ref: false });' +
    'interval[Symbol.asyncIterator]().next()' +
    '.then(assert.fail)']).then(common.mustCall(({ stderr }) => {
    assert.strictEqual(stderr, '');
  }));
}

{
  async function runInterval(fn, intervalTime, signal) {
    const input = 'foobar';
    const interval = setInterval(intervalTime, input, { signal });
    let iteration = 0;
    for await (const value of interval) {
      assert.strictEqual(value, input);
      iteration++;
      await fn(iteration);
    }
  }

  {
    // Check that we call the correct amount of times.
    const controller = new AbortController();
    const { signal } = controller;

    let loopCount = 0;
    const delay = 20;
    const timeoutLoop = runInterval(() => {
      loopCount++;
      if (loopCount === 5) controller.abort();
      if (loopCount > 5) throw new Error('ran too many times');
    }, delay, signal);

    assert.rejects(timeoutLoop, /AbortError/).then(common.mustCall(() => {
      assert.strictEqual(loopCount, 5);
    }));
  }

  {
    // Check that if we abort when we have some unresolved callbacks,
    // we actually call them.
    const controller = new AbortController();
    const { signal } = controller;
    const delay = 10;
    let totalIterations = 0;
    const timeoutLoop = runInterval(async (iterationNumber) => {
      await setPromiseTimeout(delay * 4);
      if (iterationNumber <= 2) {
        assert.strictEqual(signal.aborted, false);
      }
      if (iterationNumber === 2) {
        controller.abort();
      }
      if (iterationNumber > 2) {
        assert.strictEqual(signal.aborted, true);
      }
      if (iterationNumber > totalIterations) {
        totalIterations = iterationNumber;
      }
    }, delay, signal);

    timeoutLoop.catch(common.mustCall(() => {
      assert.ok(totalIterations >= 3, `iterations was ${totalIterations} < 3`);
    }));
  }
}

{
  // Check that the timing is correct
  let pre = false;
  let post = false;

  const time_unit = 50;
  Promise.all([
    setPromiseTimeout(1).then(() => pre = true),
    new Promise((res) => {
      const iterable = timerPromises.setInterval(time_unit * 2);
      const iterator = iterable[Symbol.asyncIterator]();

      iterator.next().then(() => {
        assert.ok(pre, 'interval ran too early');
        assert.ok(!post, 'interval ran too late');
        return iterator.next();
      }).then(() => {
        assert.ok(post, 'second interval ran too early');
        return iterator.return();
      }).then(res);
    }),
    setPromiseTimeout(time_unit * 3).then(() => post = true),
  ]).then(common.mustCall());
}

(async () => {
  const signal = AbortSignal.abort('boom');
  try {
    const iterable = timerPromises.setInterval(2, undefined, { signal });
    // eslint-disable-next-line no-unused-vars, no-empty
    for await (const _ of iterable) { }
    assert.fail('should have failed');
  } catch (err) {
    assert.strictEqual(err.cause, 'boom');
  }
})().then(common.mustCall());
