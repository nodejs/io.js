// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/rs-utils.js
'use strict';

promise_test(t => {

  const randomSource = new RandomPushSource();

  let cancellationFinished = false;
  const rs = new ReadableStream({
    start(c) {
      randomSource.ondata = c.enqueue.bind(c);
      randomSource.onend = c.close.bind(c);
      randomSource.onerror = c.error.bind(c);
    },

    pull() {
      randomSource.readStart();
    },

    cancel() {
      randomSource.readStop();

      return new Promise(resolve => {
        t.step_timeout(() => {
          cancellationFinished = true;
          resolve();
        }, 1);
      });
    }
  });

  const reader = rs.getReader();

  // We call delay multiple times to avoid cancelling too early for the
  // source to enqueue at least one chunk.
  const cancel = delay(5).then(() => delay(5)).then(() => delay(5)).then(() => {
    const cancelPromise = reader.cancel();
    assert_false(cancellationFinished, 'cancellation in source should happen later');
    return cancelPromise;
  });

  return readableStreamToArray(rs, reader).then(chunks => {
    assert_greater_than(chunks.length, 0, 'at least one chunk should be read');
    for (let i = 0; i < chunks.length; i++) {
      assert_equals(chunks[i].length, 128, 'chunk ' + i + ' should have 128 bytes');
    }
    return cancel;
  }).then(() => {
    assert_true(cancellationFinished, 'it returns a promise that is fulfilled when the cancellation finishes');
  });

}, 'ReadableStream cancellation: integration test on an infinite stream derived from a random push source');

test(() => {

  let recordedReason;
  const rs = new ReadableStream({
    cancel(reason) {
      recordedReason = reason;
    }
  });

  const passedReason = new Error('Sorry, it just wasn\'t meant to be.');
  rs.cancel(passedReason);

  assert_equals(recordedReason, passedReason,
    'the error passed to the underlying source\'s cancel method should equal the one passed to the stream\'s cancel');

}, 'ReadableStream cancellation: cancel(reason) should pass through the given reason to the underlying source');

promise_test(() => {

  const rs = new ReadableStream({
    start(c) {
      c.enqueue('a');
      c.close();
    },
    cancel() {
      assert_unreached('underlying source cancel() should not have been called');
    }
  });

  const reader = rs.getReader();

  return rs.cancel().then(() => {
    assert_unreached('cancel() should be rejected');
  }, e => {
    assert_equals(e.name, 'TypeError', 'cancel() should be rejected with a TypeError');
  }).then(() => {
    return reader.read();
  }).then(result => {
    assert_object_equals(result, { value: 'a', done: false }, 'read() should still work after the attempted cancel');
    return reader.closed;
  });

}, 'ReadableStream cancellation: cancel() on a locked stream should fail and not call the underlying source cancel');

promise_test(() => {

  let cancelReceived = false;
  const cancelReason = new Error('I am tired of this stream, I prefer to cancel it');
  const rs = new ReadableStream({
    cancel(reason) {
      cancelReceived = true;
      assert_equals(reason, cancelReason, 'cancellation reason given to the underlying source should be equal to the one passed');
    }
  });

  return rs.cancel(cancelReason).then(() => {
    assert_true(cancelReceived);
  });

}, 'ReadableStream cancellation: should fulfill promise when cancel callback went fine');

promise_test(() => {

  const rs = new ReadableStream({
    cancel() {
      return 'Hello';
    }
  });

  return rs.cancel().then(v => {
    assert_equals(v, undefined, 'cancel() return value should be fulfilled with undefined');
  });

}, 'ReadableStream cancellation: returning a value from the underlying source\'s cancel should not affect the fulfillment value of the promise returned by the stream\'s cancel');

promise_test(() => {

  const thrownError = new Error('test');
  let cancelCalled = false;

  const rs = new ReadableStream({
    cancel() {
      cancelCalled = true;
      throw thrownError;
    }
  });

  return rs.cancel('test').then(() => {
    assert_unreached('cancel should reject');
  }, e => {
    assert_true(cancelCalled);
    assert_equals(e, thrownError);
  });

}, 'ReadableStream cancellation: should reject promise when cancel callback raises an exception');

promise_test(() => {

  const cancelReason = new Error('test');

  const rs = new ReadableStream({
    cancel(error) {
      assert_equals(error, cancelReason);
      return delay(1);
    }
  });

  return rs.cancel(cancelReason);

}, 'ReadableStream cancellation: if the underlying source\'s cancel method returns a promise, the promise returned by the stream\'s cancel should fulfill when that one does (1)');

promise_test(t => {

  let resolveSourceCancelPromise;
  let sourceCancelPromiseHasFulfilled = false;

  const rs = new ReadableStream({
    cancel() {
      const sourceCancelPromise = new Promise(resolve => resolveSourceCancelPromise = resolve);

      sourceCancelPromise.then(() => {
        sourceCancelPromiseHasFulfilled = true;
      });

      return sourceCancelPromise;
    }
  });

  t.step_timeout(() => resolveSourceCancelPromise('Hello'), 1);

  return rs.cancel().then(value => {
    assert_true(sourceCancelPromiseHasFulfilled, 'cancel() return value should be fulfilled only after the promise returned by the underlying source\'s cancel');
    assert_equals(value, undefined, 'cancel() return value should be fulfilled with undefined');
  });

}, 'ReadableStream cancellation: if the underlying source\'s cancel method returns a promise, the promise returned by the stream\'s cancel should fulfill when that one does (2)');

promise_test(t => {

  let rejectSourceCancelPromise;
  let sourceCancelPromiseHasRejected = false;

  const rs = new ReadableStream({
    cancel() {
      const sourceCancelPromise = new Promise((resolve, reject) => rejectSourceCancelPromise = reject);

      sourceCancelPromise.catch(() => {
        sourceCancelPromiseHasRejected = true;
      });

      return sourceCancelPromise;
    }
  });

  const errorInCancel = new Error('Sorry, it just wasn\'t meant to be.');

  t.step_timeout(() => rejectSourceCancelPromise(errorInCancel), 1);

  return rs.cancel().then(() => {
    assert_unreached('cancel() return value should be rejected');
  }, r => {
    assert_true(sourceCancelPromiseHasRejected, 'cancel() return value should be rejected only after the promise returned by the underlying source\'s cancel');
    assert_equals(r, errorInCancel, 'cancel() return value should be rejected with the underlying source\'s rejection reason');
  });

}, 'ReadableStream cancellation: if the underlying source\'s cancel method returns a promise, the promise returned by the stream\'s cancel should reject when that one does');

promise_test(() => {

  const rs = new ReadableStream({
    start() {
      return new Promise(() => {});
    },
    pull() {
      assert_unreached('pull should not have been called');
    }
  });

  return Promise.all([rs.cancel(), rs.getReader().closed]);

}, 'ReadableStream cancellation: cancelling before start finishes should prevent pull() from being called');
