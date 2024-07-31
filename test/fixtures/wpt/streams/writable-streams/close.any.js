// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
'use strict';

const error1 = new Error('error1');
error1.name = 'error1';

const error2 = new Error('error2');
error2.name = 'error2';

promise_test(() => {
  const ws = new WritableStream({
    close() {
      return 'Hello';
    }
  });

  const writer = ws.getWriter();

  const closePromise = writer.close();
  return closePromise.then(value => assert_equals(value, undefined, 'fulfillment value must be undefined'));
}, 'fulfillment value of writer.close() call must be undefined even if the underlying sink returns a non-undefined ' +
    'value');

promise_test(() => {
  let controller;
  let resolveClose;
  const ws = new WritableStream({
    start(c) {
      controller = c;
    },
    close() {
      return new Promise(resolve => {
        resolveClose = resolve;
      });
    }
  });

  const writer = ws.getWriter();

  const closePromise = writer.close();
  return flushAsyncEvents().then(() => {
    controller.error(error1);
    return flushAsyncEvents();
  }).then(() => {
    resolveClose();
    return Promise.all([
      closePromise,
      writer.closed,
      flushAsyncEvents().then(() => writer.closed)]);
  });
}, 'when sink calls error asynchronously while sink close is in-flight, the stream should not become errored');

promise_test(() => {
  let controller;
  const passedError = new Error('error me');
  const ws = new WritableStream({
    start(c) {
      controller = c;
    },
    close() {
      controller.error(passedError);
    }
  });

  const writer = ws.getWriter();

  return writer.close().then(() => writer.closed);
}, 'when sink calls error synchronously while closing, the stream should not become errored');

promise_test(t => {
  const ws = new WritableStream({
    close() {
      throw error1;
    }
  });

  const writer = ws.getWriter();

  return Promise.all([
    writer.write('y'),
    promise_rejects_exactly(t, error1, writer.close(), 'close() must reject with the error'),
    promise_rejects_exactly(t, error1, writer.closed, 'closed must reject with the error')
  ]);
}, 'when the sink throws during close, and the close is requested while a write is still in-flight, the stream should ' +
   'become errored during the close');

promise_test(() => {
  const ws = new WritableStream({
    write(chunk, controller) {
      controller.error(error1);
      return new Promise(() => {});
    }
  });

  const writer = ws.getWriter();
  writer.write('a');

  return delay(0).then(() => {
    writer.releaseLock();
  });
}, 'releaseLock on a stream with a pending write in which the stream has been errored');

promise_test(() => {
  let controller;
  const ws = new WritableStream({
    start(c) {
      controller = c;
    },
    close() {
      controller.error(error1);
      return new Promise(() => {});
    }
  });

  const writer = ws.getWriter();
  writer.close();

  return delay(0).then(() => {
    writer.releaseLock();
  });
}, 'releaseLock on a stream with a pending close in which controller.error() was called');

promise_test(() => {
  const ws = recordingWritableStream();

  const writer = ws.getWriter();

  return writer.ready.then(() => {
    assert_equals(writer.desiredSize, 1, 'desiredSize should be 1');

    writer.close();
    assert_equals(writer.desiredSize, 1, 'desiredSize should be still 1');

    return writer.ready.then(v => {
      assert_equals(v, undefined, 'ready promise should be fulfilled with undefined');
      assert_array_equals(ws.events, ['close'], 'write and abort should not be called');
    });
  });
}, 'when close is called on a WritableStream in writable state, ready should return a fulfilled promise');

promise_test(() => {
  const ws = recordingWritableStream({
    write() {
      return new Promise(() => {});
    }
  });

  const writer = ws.getWriter();

  return writer.ready.then(() => {
    writer.write('a');

    assert_equals(writer.desiredSize, 0, 'desiredSize should be 0');

    let calledClose = false;
    return Promise.all([
      writer.ready.then(v => {
        assert_equals(v, undefined, 'ready promise should be fulfilled with undefined');
        assert_true(calledClose, 'ready should not be fulfilled before writer.close() is called');
        assert_array_equals(ws.events, ['write', 'a'], 'sink abort() should not be called');
      }),
      flushAsyncEvents().then(() => {
        writer.close();
        calledClose = true;
      })
    ]);
  });
}, 'when close is called on a WritableStream in waiting state, ready promise should be fulfilled');

promise_test(() => {
  let asyncCloseFinished = false;
  const ws = recordingWritableStream({
    close() {
      return flushAsyncEvents().then(() => {
        asyncCloseFinished = true;
      });
    }
  });

  const writer = ws.getWriter();
  return writer.ready.then(() => {
    writer.write('a');

    writer.close();

    return writer.ready.then(v => {
      assert_false(asyncCloseFinished, 'ready promise should be fulfilled before async close completes');
      assert_equals(v, undefined, 'ready promise should be fulfilled with undefined');
      assert_array_equals(ws.events, ['write', 'a', 'close'], 'sink abort() should not be called');
    });
  });
}, 'when close is called on a WritableStream in waiting state, ready should be fulfilled immediately even if close ' +
    'takes a long time');

promise_test(t => {
  const rejection = { name: 'letter' };
  const ws = new WritableStream({
    close() {
      return {
        then(onFulfilled, onRejected) { onRejected(rejection); }
      };
    }
  });
  return promise_rejects_exactly(t, rejection, ws.getWriter().close(), 'close() should return a rejection');
}, 'returning a thenable from close() should work');

promise_test(t => {
  const ws = new WritableStream();
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const closePromise = writer.close();
    const closedPromise = writer.closed;
    writer.releaseLock();
    return Promise.all([
      closePromise,
      promise_rejects_js(t, TypeError, closedPromise, '.closed promise should be rejected')
    ]);
  });
}, 'releaseLock() should not change the result of sync close()');

promise_test(t => {
  const ws = new WritableStream({
    close() {
      return flushAsyncEvents();
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const closePromise = writer.close();
    const closedPromise = writer.closed;
    writer.releaseLock();
    return Promise.all([
      closePromise,
      promise_rejects_js(t, TypeError, closedPromise, '.closed promise should be rejected')
    ]);
  });
}, 'releaseLock() should not change the result of async close()');

promise_test(() => {
  let resolveClose;
  const ws = new WritableStream({
    close() {
      const promise = new Promise(resolve => {
        resolveClose = resolve;
      });
      return promise;
    }
  });
  const writer = ws.getWriter();
  const closePromise = writer.close();
  writer.releaseLock();
  return delay(0).then(() => {
    resolveClose();
    return closePromise.then(() => {
      assert_equals(ws.getWriter().desiredSize, 0, 'desiredSize should be 0');
    });
  });
}, 'close() should set state to CLOSED even if writer has detached');

promise_test(() => {
  let resolveClose;
  const ws = new WritableStream({
    close() {
      const promise = new Promise(resolve => {
        resolveClose = resolve;
      });
      return promise;
    }
  });
  const writer = ws.getWriter();
  writer.close();
  writer.releaseLock();
  return delay(0).then(() => {
    const abortingWriter = ws.getWriter();
    const abortPromise = abortingWriter.abort();
    abortingWriter.releaseLock();
    resolveClose();
    return abortPromise;
  });
}, 'the promise returned by async abort during close should resolve');

// Though the order in which the promises are fulfilled or rejected is arbitrary, we're checking it for
// interoperability. We can change the order as long as we file bugs on all implementers to update to the latest tests
// to keep them interoperable.

promise_test(() => {
  const ws = new WritableStream({});

  const writer = ws.getWriter();

  const closePromise = writer.close();

  const events = [];
  return Promise.all([
    closePromise.then(() => {
      events.push('closePromise');
    }),
    writer.closed.then(() => {
      events.push('closed');
    })
  ]).then(() => {
    assert_array_equals(events, ['closePromise', 'closed'],
                        'promises must fulfill/reject in the expected order');
  });
}, 'promises must fulfill/reject in the expected order on closure');

promise_test(() => {
  const ws = new WritableStream({});

  // Wait until the WritableStream starts so that the close() call gets processed. Otherwise, abort() will be
  // processed without waiting for completion of the close().
  return delay(0).then(() => {
    const writer = ws.getWriter();

    const closePromise = writer.close();
    const abortPromise = writer.abort(error1);

    const events = [];
    return Promise.all([
      closePromise.then(() => {
        events.push('closePromise');
      }),
      abortPromise.then(() => {
        events.push('abortPromise');
      }),
      writer.closed.then(() => {
        events.push('closed');
      })
    ]).then(() => {
      assert_array_equals(events, ['closePromise', 'abortPromise', 'closed'],
                          'promises must fulfill/reject in the expected order');
    });
  });
}, 'promises must fulfill/reject in the expected order on aborted closure');

promise_test(t => {
  const ws = new WritableStream({
    close() {
      return Promise.reject(error1);
    }
  });

  // Wait until the WritableStream starts so that the close() call gets processed.
  return delay(0).then(() => {
    const writer = ws.getWriter();

    const closePromise = writer.close();
    const abortPromise = writer.abort(error2);

    const events = [];
    closePromise.catch(() => events.push('closePromise'));
    abortPromise.catch(() => events.push('abortPromise'));
    writer.closed.catch(() => events.push('closed'));
    return Promise.all([
      promise_rejects_exactly(t, error1, closePromise,
                             'closePromise must reject with the error returned from the sink\'s close method'),
      promise_rejects_exactly(t, error1, abortPromise,
                             'abortPromise must reject with the error returned from the sink\'s close method'),
      promise_rejects_exactly(t, error2, writer.closed,
                              'writer.closed must reject with error2')
    ]).then(() => {
      assert_array_equals(events, ['closePromise', 'abortPromise', 'closed'],
                          'promises must fulfill/reject in the expected order');
    });
  });
}, 'promises must fulfill/reject in the expected order on aborted and errored closure');

promise_test(t => {
  let resolveWrite;
  let controller;
  const ws = new WritableStream({
    write(chunk, c) {
      controller = c;
      return new Promise(resolve => {
        resolveWrite = resolve;
      });
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const writePromise = writer.write('c');
    controller.error(error1);
    const closePromise = writer.close();
    let closeRejected = false;
    closePromise.catch(() => {
      closeRejected = true;
    });
    return flushAsyncEvents().then(() => {
      assert_false(closeRejected);
      resolveWrite();
      return Promise.all([
        writePromise,
        promise_rejects_exactly(t, error1, closePromise, 'close() should reject')
      ]).then(() => {
        assert_true(closeRejected);
      });
    });
  });
}, 'close() should not reject until no sink methods are in flight');

promise_test(() => {
  const ws = new WritableStream();
  const writer1 = ws.getWriter();
  return writer1.close().then(() => {
    writer1.releaseLock();
    const writer2 = ws.getWriter();
    const ready = writer2.ready;
    assert_equals(ready.constructor, Promise);
    return ready;
  });
}, 'ready promise should be initialised as fulfilled for a writer on a closed stream');

promise_test(() => {
  const ws = new WritableStream();
  ws.close();
  const writer = ws.getWriter();
  return writer.closed;
}, 'close() on a writable stream should work');

promise_test(t => {
  const ws = new WritableStream();
  ws.getWriter();
  return promise_rejects_js(t, TypeError, ws.close(), 'close should reject');
}, 'close() on a locked stream should reject');

promise_test(t => {
  const ws = new WritableStream({
    start(controller) {
      controller.error(error1);
    }
  });
  return promise_rejects_exactly(t, error1, ws.close(), 'close should reject with error1');
}, 'close() on an erroring stream should reject');

promise_test(t => {
  const ws = new WritableStream({
    start(controller) {
      controller.error(error1);
    }
  });
  const writer = ws.getWriter();
  return promise_rejects_exactly(t, error1, writer.closed, 'closed should reject with the error').then(() => {
    writer.releaseLock();
    return promise_rejects_js(t, TypeError, ws.close(), 'close should reject');
  });
}, 'close() on an errored stream should reject');

promise_test(t => {
  const ws = new WritableStream();
  const writer = ws.getWriter();
  return writer.close().then(() => {
    return promise_rejects_js(t, TypeError, ws.close(), 'close should reject');
  });
}, 'close() on an closed stream should reject');

promise_test(t => {
  const ws = new WritableStream({
    close() {
      return new Promise(() => {});
    }
  });

  const writer = ws.getWriter();
  writer.close();
  writer.releaseLock();

  return promise_rejects_js(t, TypeError, ws.close(), 'close should reject');
}, 'close() on a stream with a pending close should reject');
