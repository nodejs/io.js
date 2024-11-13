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

assert.strictEqual(setPromiseTimeout, timerPromises.setTimeout);

process.on('multipleResolves', common.mustNotCall());

{
  const promise = setPromiseTimeout(1);
  promise.then(common.mustCall((value) => {
    assert.strictEqual(value, undefined);
  }));
}

{
  const promise = setPromiseTimeout(1, 'foobar');
  promise.then(common.mustCall((value) => {
    assert.strictEqual(value, 'foobar');
  }));
}

{
  const ac = new AbortController();
  const signal = ac.signal;
  assert.rejects(setPromiseTimeout(10, undefined, { signal }), /AbortError/)
    .then(common.mustCall());
  ac.abort();
}

{
  const signal = AbortSignal.abort(); // Abort in advance
  assert.rejects(setPromiseTimeout(10, undefined, { signal }), /AbortError/)
    .then(common.mustCall());
}

{
  // Check that aborting after resolve will not reject.
  const ac = new AbortController();
  const signal = ac.signal;
  setPromiseTimeout(10, undefined, { signal })
    .then(common.mustCall(() => { ac.abort(); }))
    .then(common.mustCall());
}

{
  // Check that timer adding signals does not leak handlers
  const signal = new NodeEventTarget();
  signal.aborted = false;
  setPromiseTimeout(0, null, { signal }).finally(common.mustCall(() => {
    assert.strictEqual(getEventListeners(signal, 'abort').length, 0);
  }));
}

{
  for (const delay of ['', false]) {
    assert.rejects(() => setPromiseTimeout(delay, null, {}), /ERR_INVALID_ARG_TYPE/).then(common.mustCall());
  }

  for (const options of [1, '', false, Infinity]) {
    assert.rejects(() => setPromiseTimeout(10, null, options), /ERR_INVALID_ARG_TYPE/).then(common.mustCall());
  }

  for (const signal of [1, '', false, Infinity, null, {}]) {
    assert.rejects(() => setPromiseTimeout(10, null, { signal }), /ERR_INVALID_ARG_TYPE/).then(common.mustCall());
  }

  for (const ref of [1, '', Infinity, null, {}]) {
    assert.rejects(() => setPromiseTimeout(10, null, { ref }), /ERR_INVALID_ARG_TYPE/).then(common.mustCall());
  }
}

{
  common.spawnPromisified(process.execPath, ['-pe', 'const assert = require(\'assert\');' +
    'require(\'timers/promises\').setTimeout(1000, null, { ref: false }).' +
    'then(assert.fail)']).then(common.mustCall(({ stderr }) => {
    assert.strictEqual(stderr, '');
  }));
}

(async () => {
  const signal = AbortSignal.abort('boom');
  await assert.rejects(timerPromises.setTimeout(1, undefined, { signal }), {
    cause: 'boom',
  });
})().then(common.mustCall());
