// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
'use strict';

const thrownError = new Error('bad things are happening!');
thrownError.name = 'error1';

const originalReason = new Error('original reason');
originalReason.name = 'error2';

promise_test(async t => {
  let cancelled = undefined;
  const ts = new TransformStream({
    cancel(reason) {
      cancelled = reason;
    }
  });
  const res = await ts.readable.cancel(thrownError);
  assert_equals(res, undefined, 'readable.cancel() should return undefined');
  assert_equals(cancelled, thrownError, 'transformer.cancel() should be called with the passed reason');
}, 'cancelling the readable side should call transformer.cancel()');

promise_test(async t => {
  const ts = new TransformStream({
    cancel(reason) {
      assert_equals(reason, originalReason, 'transformer.cancel() should be called with the passed reason');
      throw thrownError;
    }
  });
  const writer = ts.writable.getWriter();
  const cancelPromise = ts.readable.cancel(originalReason);
  await promise_rejects_exactly(t, thrownError, cancelPromise, 'readable.cancel() should reject with thrownError');
  await promise_rejects_exactly(t, thrownError, writer.closed, 'writer.closed should reject with thrownError');
}, 'cancelling the readable side should reject if transformer.cancel() throws');

promise_test(async t => {
  let aborted = undefined;
  const ts = new TransformStream({
    cancel(reason) {
      aborted = reason;
    },
    flush: t.unreached_func('flush should not be called')
  });
  const res = await ts.writable.abort(thrownError);
  assert_equals(res, undefined, 'writable.abort() should return undefined');
  assert_equals(aborted, thrownError, 'transformer.abort() should be called with the passed reason');
}, 'aborting the writable side should call transformer.abort()');

promise_test(async t => {
  const ts = new TransformStream({
    cancel(reason) {
      assert_equals(reason, originalReason, 'transformer.cancel() should be called with the passed reason');
      throw thrownError;
    },
    flush: t.unreached_func('flush should not be called')
  });
  const reader = ts.readable.getReader();
  const abortPromise = ts.writable.abort(originalReason);
  await promise_rejects_exactly(t, thrownError, abortPromise, 'writable.abort() should reject with thrownError');
  await promise_rejects_exactly(t, thrownError, reader.closed, 'reader.closed should reject with thrownError');
}, 'aborting the writable side should reject if transformer.cancel() throws');

promise_test(async t => {
  const ts = new TransformStream({
    async cancel(reason) {
      assert_equals(reason, originalReason, 'transformer.cancel() should be called with the passed reason');
      throw thrownError;
    },
    flush: t.unreached_func('flush should not be called')
  });
  const cancelPromise = ts.readable.cancel(originalReason);
  const closePromise = ts.writable.close();
  await Promise.all([
    promise_rejects_exactly(t, thrownError, cancelPromise, 'cancelPromise should reject with thrownError'),
    promise_rejects_exactly(t, thrownError, closePromise, 'closePromise should reject with thrownError'),
  ]);
}, 'closing the writable side should reject if a parallel transformer.cancel() throws');

promise_test(async t => {
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    },
    async cancel(reason) {
      assert_equals(reason, originalReason, 'transformer.cancel() should be called with the passed reason');
      controller.error(thrownError);
    },
    flush: t.unreached_func('flush should not be called')
  });
  const cancelPromise = ts.readable.cancel(originalReason);
  const closePromise = ts.writable.close();
  await Promise.all([
    promise_rejects_exactly(t, thrownError, cancelPromise, 'cancelPromise should reject with thrownError'),
    promise_rejects_exactly(t, thrownError, closePromise, 'closePromise should reject with thrownError'),
  ]);
}, 'readable.cancel() and a parallel writable.close() should reject if a transformer.cancel() calls controller.error()');

promise_test(async t => {
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    },
    async cancel(reason) {
      assert_equals(reason, originalReason, 'transformer.cancel() should be called with the passed reason');
      controller.error(thrownError);
    },
    flush: t.unreached_func('flush should not be called')
  });
  const cancelPromise = ts.writable.abort(originalReason);
  await promise_rejects_exactly(t, thrownError, cancelPromise, 'cancelPromise should reject with thrownError');
  const closePromise = ts.readable.cancel(1);
  await promise_rejects_exactly(t, thrownError, closePromise, 'closePromise should reject with thrownError');
}, 'writable.abort() and readable.cancel() should reject if a transformer.cancel() calls controller.error()');
