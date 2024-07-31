// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
'use strict';

const thrownError = new Error('bad things are happening!');
thrownError.name = 'error1';

promise_test(t => {
  const ts = new TransformStream({
    transform() {
      throw thrownError;
    },
    cancel: t.unreached_func('cancel should not be called')
  });

  const reader = ts.readable.getReader();

  const writer = ts.writable.getWriter();

  return Promise.all([
    promise_rejects_exactly(t, thrownError, writer.write('a'),
                            'writable\'s write should reject with the thrown error'),
    promise_rejects_exactly(t, thrownError, reader.read(),
                            'readable\'s read should reject with the thrown error'),
    promise_rejects_exactly(t, thrownError, reader.closed,
                            'readable\'s closed should be rejected with the thrown error'),
    promise_rejects_exactly(t, thrownError, writer.closed,
                            'writable\'s closed should be rejected with the thrown error')
  ]);
}, 'TransformStream errors thrown in transform put the writable and readable in an errored state');

promise_test(t => {
  const ts = new TransformStream({
    transform() {
    },
    flush() {
      throw thrownError;
    },
    cancel: t.unreached_func('cancel should not be called')
  });

  const reader = ts.readable.getReader();

  const writer = ts.writable.getWriter();

  return Promise.all([
    writer.write('a'),
    promise_rejects_exactly(t, thrownError, writer.close(),
                            'writable\'s close should reject with the thrown error'),
    promise_rejects_exactly(t, thrownError, reader.read(),
                            'readable\'s read should reject with the thrown error'),
    promise_rejects_exactly(t, thrownError, reader.closed,
                            'readable\'s closed should be rejected with the thrown error'),
    promise_rejects_exactly(t, thrownError, writer.closed,
                            'writable\'s closed should be rejected with the thrown error')
  ]);
}, 'TransformStream errors thrown in flush put the writable and readable in an errored state');

test(t => {
  new TransformStream({
    start(c) {
      c.enqueue('a');
      c.error(new Error('generic error'));
      assert_throws_js(TypeError, () => c.enqueue('b'), 'enqueue() should throw');
    },
    cancel: t.unreached_func('cancel should not be called')
  });
}, 'errored TransformStream should not enqueue new chunks');

promise_test(t => {
  const ts = new TransformStream({
    start() {
      return flushAsyncEvents().then(() => {
        throw thrownError;
      });
    },
    transform: t.unreached_func('transform should not be called'),
    flush: t.unreached_func('flush should not be called'),
    cancel: t.unreached_func('cancel should not be called')
  });

  const writer = ts.writable.getWriter();
  const reader = ts.readable.getReader();
  return Promise.all([
    promise_rejects_exactly(t, thrownError, writer.write('a'), 'writer should reject with thrownError'),
    promise_rejects_exactly(t, thrownError, writer.close(), 'close() should reject with thrownError'),
    promise_rejects_exactly(t, thrownError, reader.read(), 'reader should reject with thrownError')
  ]);
}, 'TransformStream transformer.start() rejected promise should error the stream');

promise_test(t => {
  const controllerError = new Error('start failure');
  controllerError.name = 'controllerError';
  const ts = new TransformStream({
    start(c) {
      return flushAsyncEvents()
        .then(() => {
          c.error(controllerError);
          throw new Error('ignored error');
        });
    },
    transform: t.unreached_func('transform should never be called if start() fails'),
    flush: t.unreached_func('flush should never be called if start() fails'),
    cancel: t.unreached_func('cancel should never be called if start() fails')
  });

  const writer = ts.writable.getWriter();
  const reader = ts.readable.getReader();
  return Promise.all([
    promise_rejects_exactly(t, controllerError, writer.write('a'), 'writer should reject with controllerError'),
    promise_rejects_exactly(t, controllerError, writer.close(), 'close should reject with same error'),
    promise_rejects_exactly(t, controllerError, reader.read(), 'reader should reject with same error')
  ]);
}, 'when controller.error is followed by a rejection, the error reason should come from controller.error');

test(() => {
  assert_throws_js(URIError, () => new TransformStream({
    start() { throw new URIError('start thrown error'); },
    transform() {}
  }), 'constructor should throw');
}, 'TransformStream constructor should throw when start does');

test(() => {
  const strategy = {
    size() { throw new URIError('size thrown error'); }
  };

  assert_throws_js(URIError, () => new TransformStream({
    start(c) {
      c.enqueue('a');
    },
    transform() {}
  }, undefined, strategy), 'constructor should throw the same error strategy.size throws');
}, 'when strategy.size throws inside start(), the constructor should throw the same error');

test(() => {
  const controllerError = new URIError('controller.error');

  let controller;
  const strategy = {
    size() {
      controller.error(controllerError);
      throw new Error('redundant error');
    }
  };

  assert_throws_js(URIError, () => new TransformStream({
    start(c) {
      controller = c;
      c.enqueue('a');
    },
    transform() {}
  }, undefined, strategy), 'the first error should be thrown');
}, 'when strategy.size calls controller.error() then throws, the constructor should throw the first error');

promise_test(t => {
  const ts = new TransformStream();
  const writer = ts.writable.getWriter();
  const closedPromise = writer.closed;
  return Promise.all([
    ts.readable.cancel(thrownError),
    promise_rejects_exactly(t, thrownError, closedPromise, 'closed should throw a TypeError')
  ]);
}, 'cancelling the readable side should error the writable');

promise_test(t => {
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  });
  const writer = ts.writable.getWriter();
  const reader = ts.readable.getReader();
  const writePromise = writer.write('a');
  const closePromise = writer.close();
  controller.error(thrownError);
  return Promise.all([
    promise_rejects_exactly(t, thrownError, reader.closed, 'reader.closed should reject'),
    promise_rejects_exactly(t, thrownError, writePromise, 'writePromise should reject'),
    promise_rejects_exactly(t, thrownError, closePromise, 'closePromise should reject')]);
}, 'it should be possible to error the readable between close requested and complete');

promise_test(t => {
  const ts = new TransformStream({
    transform(chunk, controller) {
      controller.enqueue(chunk);
      controller.terminate();
      throw thrownError;
    }
  }, undefined, { highWaterMark: 1 });
  const writePromise = ts.writable.getWriter().write('a');
  const closedPromise = ts.readable.getReader().closed;
  return Promise.all([
    promise_rejects_exactly(t, thrownError, writePromise, 'write() should reject'),
    promise_rejects_exactly(t, thrownError, closedPromise, 'reader.closed should reject')
  ]);
}, 'an exception from transform() should error the stream if terminate has been requested but not completed');

promise_test(t => {
  const ts = new TransformStream();
  const writer = ts.writable.getWriter();
  // The microtask following transformer.start() hasn't completed yet, so the abort is queued and not notified to the
  // TransformStream yet.
  const abortPromise = writer.abort(thrownError);
  const cancelPromise = ts.readable.cancel(new Error('cancel reason'));
  return Promise.all([
    abortPromise,
    cancelPromise,
    promise_rejects_exactly(t, thrownError, writer.closed, 'writer.closed should reject'),
  ]);
}, 'abort should set the close reason for the writable when it happens before cancel during start, and cancel should ' +
             'reject');

promise_test(t => {
  let resolveTransform;
  const transformPromise = new Promise(resolve => {
    resolveTransform = resolve;
  });
  const ts = new TransformStream({
    transform() {
      return transformPromise;
    }
  }, undefined, { highWaterMark: 2 });
  const writer = ts.writable.getWriter();
  return delay(0).then(() => {
    const writePromise = writer.write();
    const abortPromise = writer.abort(thrownError);
    const cancelPromise = ts.readable.cancel(new Error('cancel reason'));
    resolveTransform();
    return Promise.all([
      writePromise,
      abortPromise,
      cancelPromise,
      promise_rejects_exactly(t, thrownError, writer.closed, 'writer.closed should reject with thrownError')]);
  });
}, 'abort should set the close reason for the writable when it happens before cancel during underlying sink write, ' +
             'but cancel should still succeed');

const ignoredError = new Error('ignoredError');
ignoredError.name = 'ignoredError';

promise_test(t => {
  const ts = new TransformStream({
    start(controller) {
      controller.error(thrownError);
      controller.error(ignoredError);
    }
  });
  return promise_rejects_exactly(t, thrownError, ts.writable.abort(), 'abort() should reject with thrownError');
}, 'controller.error() should do nothing the second time it is called');

promise_test(t => {
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  });
  const cancelPromise = ts.readable.cancel(ignoredError);
  controller.error(thrownError);
  return Promise.all([
    cancelPromise,
    promise_rejects_exactly(t, thrownError, ts.writable.getWriter().closed, 'closed should reject with thrownError')
  ]);
}, 'controller.error() should close writable immediately after readable.cancel()');

promise_test(t => {
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  });
  return ts.readable.cancel(thrownError).then(() => {
    controller.error(ignoredError);
    return promise_rejects_exactly(t, thrownError, ts.writable.getWriter().closed, 'closed should reject with thrownError');
  });
}, 'controller.error() should do nothing after readable.cancel() resolves');

promise_test(t => {
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  });
  return ts.writable.abort(thrownError).then(() => {
    controller.error(ignoredError);
    return promise_rejects_exactly(t, thrownError, ts.writable.getWriter().closed, 'closed should reject with thrownError');
  });
}, 'controller.error() should do nothing after writable.abort() has completed');

promise_test(t => {
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    },
    transform() {
      throw thrownError;
    }
  }, undefined, { highWaterMark: Infinity });
  const writer = ts.writable.getWriter();
  return promise_rejects_exactly(t, thrownError, writer.write(), 'write() should reject').then(() => {
    controller.error();
    return promise_rejects_exactly(t, thrownError, writer.closed, 'closed should reject with thrownError');
  });
}, 'controller.error() should do nothing after a transformer method has thrown an exception');

promise_test(t => {
  let controller;
  let calls = 0;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    },
    transform() {
      ++calls;
    }
  }, undefined, { highWaterMark: 1 });
  return delay(0).then(() => {
    // Create backpressure.
    controller.enqueue('a');
    const writer = ts.writable.getWriter();
    // transform() will not be called until backpressure is relieved.
    const writePromise = writer.write('b');
    assert_equals(calls, 0, 'transform() should not have been called');
    controller.error(thrownError);
    // Now backpressure has been relieved and the write can proceed.
    return promise_rejects_exactly(t, thrownError, writePromise, 'write() should reject').then(() => {
      assert_equals(calls, 0, 'transform() should not be called');
    });
  });
}, 'erroring during write with backpressure should result in the write failing');

promise_test(t => {
  const ts = new TransformStream({}, undefined, { highWaterMark: 0 });
  return delay(0).then(() => {
    const writer = ts.writable.getWriter();
    // write should start synchronously
    const writePromise = writer.write(0);
    // The underlying sink's abort() is not called until the write() completes.
    const abortPromise = writer.abort(thrownError);
    // Perform a read to relieve backpressure and permit the write() to complete.
    const readPromise = ts.readable.getReader().read();
    return Promise.all([
      promise_rejects_exactly(t, thrownError, readPromise, 'read() should reject'),
      promise_rejects_exactly(t, thrownError, writePromise, 'write() should reject'),
      abortPromise
    ]);
  });
}, 'a write() that was waiting for backpressure should reject if the writable is aborted');

promise_test(t => {
  const ts = new TransformStream();
  ts.writable.abort(thrownError);
  const reader = ts.readable.getReader();
  return promise_rejects_exactly(t, thrownError, reader.read(), 'read() should reject with thrownError');
}, 'the readable should be errored with the reason passed to the writable abort() method');
