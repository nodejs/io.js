// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
'use strict';

const error1 = { name: 'error1' };

promise_test(() => {
  let resolveStartPromise;
  const ws = recordingWritableStream({
    start() {
      return new Promise(resolve => {
        resolveStartPromise = resolve;
      });
    }
  });

  const writer = ws.getWriter();

  assert_equals(writer.desiredSize, 1, 'desiredSize should be 1');
  writer.write('a');
  assert_equals(writer.desiredSize, 0, 'desiredSize should be 0 after writer.write()');

  // Wait and verify that write isn't called.
  return flushAsyncEvents()
      .then(() => {
        assert_array_equals(ws.events, [], 'write should not be called until start promise resolves');
        resolveStartPromise();
        return writer.ready;
      })
      .then(() => assert_array_equals(ws.events, ['write', 'a'],
                                      'write should not be called until start promise resolves'));
}, 'underlying sink\'s write should not be called until start finishes');

promise_test(() => {
  let resolveStartPromise;
  const ws = recordingWritableStream({
    start() {
      return new Promise(resolve => {
        resolveStartPromise = resolve;
      });
    }
  });

  const writer = ws.getWriter();

  writer.close();
  assert_equals(writer.desiredSize, 1, 'desiredSize should be 1');

  // Wait and verify that write isn't called.
  return flushAsyncEvents().then(() => {
    assert_array_equals(ws.events, [], 'close should not be called until start promise resolves');
    resolveStartPromise();
    return writer.closed;
  });
}, 'underlying sink\'s close should not be called until start finishes');

test(() => {
  const passedError = new Error('horrible things');

  let writeCalled = false;
  let closeCalled = false;
  assert_throws_exactly(passedError, () => {
    // recordingWritableStream cannot be used here because the exception in the
    // constructor prevents assigning the object to a variable.
    new WritableStream({
      start() {
        throw passedError;
      },
      write() {
        writeCalled = true;
      },
      close() {
        closeCalled = true;
      }
    });
  }, 'constructor should throw passedError');
  assert_false(writeCalled, 'write should not be called');
  assert_false(closeCalled, 'close should not be called');
}, 'underlying sink\'s write or close should not be called if start throws');

promise_test(() => {
  const ws = recordingWritableStream({
    start() {
      return Promise.reject();
    }
  });

  // Wait and verify that write or close aren't called.
  return flushAsyncEvents()
      .then(() => assert_array_equals(ws.events, [], 'write and close should not be called'));
}, 'underlying sink\'s write or close should not be invoked if the promise returned by start is rejected');

promise_test(t => {
  const ws = new WritableStream({
    start() {
      return {
        then(onFulfilled, onRejected) { onRejected(error1); }
      };
    }
  });
  return promise_rejects_exactly(t, error1, ws.getWriter().closed, 'closed promise should be rejected');
}, 'returning a thenable from start() should work');

promise_test(t => {
  const ws = recordingWritableStream({
    start(controller) {
      controller.error(error1);
    }
  });
  return promise_rejects_exactly(t, error1, ws.getWriter().write('a'), 'write() should reject with the error')
      .then(() => {
        assert_array_equals(ws.events, [], 'sink write() should not have been called');
      });
}, 'controller.error() during start should cause writes to fail');

promise_test(t => {
  let controller;
  let resolveStart;
  const ws = recordingWritableStream({
    start(c) {
      controller = c;
      return new Promise(resolve => {
        resolveStart = resolve;
      });
    }
  });
  const writer = ws.getWriter();
  const writePromise = writer.write('a');
  const closePromise = writer.close();
  controller.error(error1);
  resolveStart();
  return Promise.all([
    promise_rejects_exactly(t, error1, writePromise, 'write() should fail'),
    promise_rejects_exactly(t, error1, closePromise, 'close() should fail')
  ]).then(() => {
    assert_array_equals(ws.events, [], 'sink write() and close() should not have been called');
  });
}, 'controller.error() during async start should cause existing writes to fail');

promise_test(t => {
  const events = [];
  const promises = [];
  function catchAndRecord(promise, name) {
    promises.push(promise.then(t.unreached_func(`promise ${name} should not resolve`),
                               () => {
                                 events.push(name);
                               }));
  }
  const ws = new WritableStream({
    start() {
      return Promise.reject();
    }
  }, { highWaterMark: 0 });
  const writer = ws.getWriter();
  catchAndRecord(writer.ready, 'ready');
  catchAndRecord(writer.closed, 'closed');
  catchAndRecord(writer.write(), 'write');
  return Promise.all(promises)
      .then(() => {
        assert_array_equals(events, ['ready', 'write', 'closed'], 'promises should reject in standard order');
      });
}, 'when start() rejects, writer promises should reject in standard order');
