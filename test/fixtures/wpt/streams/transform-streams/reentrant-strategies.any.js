// META: global=window,worker,shadowrealm
// META: script=../resources/recording-streams.js
// META: script=../resources/rs-utils.js
// META: script=../resources/test-utils.js
'use strict';

// The size() function of readableStrategy can re-entrantly call back into the TransformStream implementation. This
// makes it risky to cache state across the call to ReadableStreamDefaultControllerEnqueue. These tests attempt to catch
// such errors. They are separated from the other strategy tests because no real user code should ever do anything like
// this.
//
// There is no such issue with writableStrategy size() because it is never called from within TransformStream
// algorithms.

const error1 = new Error('error1');
error1.name = 'error1';

promise_test(() => {
  let controller;
  let calls = 0;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  }, undefined, {
    size() {
      ++calls;
      if (calls < 2) {
        controller.enqueue('b');
      }
      return 1;
    },
    highWaterMark: Infinity
  });
  const writer = ts.writable.getWriter();
  return Promise.all([writer.write('a'), writer.close()])
      .then(() => readableStreamToArray(ts.readable))
      .then(array => assert_array_equals(array, ['b', 'a'], 'array should contain two chunks'));
}, 'enqueue() inside size() should work');

promise_test(() => {
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  }, undefined, {
    size() {
      // The readable queue is empty.
      controller.terminate();
      // The readable state has gone from "readable" to "closed".
      return 1;
      // This chunk will be enqueued, but will be impossible to read because the state is already "closed".
    },
    highWaterMark: Infinity
  });
  const writer = ts.writable.getWriter();
  return writer.write('a')
      .then(() => readableStreamToArray(ts.readable))
      .then(array => assert_array_equals(array, [], 'array should contain no chunks'));
  // The chunk 'a' is still in readable's queue. readable is closed so 'a' cannot be read. writable's queue is empty and
  // it is still writable.
}, 'terminate() inside size() should work');

promise_test(t => {
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  }, undefined, {
    size() {
      controller.error(error1);
      return 1;
    },
    highWaterMark: Infinity
  });
  const writer = ts.writable.getWriter();
  return writer.write('a')
      .then(() => promise_rejects_exactly(t, error1, ts.readable.getReader().read(), 'read() should reject'));
}, 'error() inside size() should work');

promise_test(() => {
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  }, undefined, {
    size() {
      assert_equals(controller.desiredSize, 1, 'desiredSize should be 1');
      return 1;
    },
    highWaterMark: 1
  });
  const writer = ts.writable.getWriter();
  return Promise.all([writer.write('a'), writer.close()])
      .then(() => readableStreamToArray(ts.readable))
      .then(array => assert_array_equals(array, ['a'], 'array should contain one chunk'));
}, 'desiredSize inside size() should work');

promise_test(t => {
  let cancelPromise;
  const ts = new TransformStream({}, undefined, {
    size() {
      cancelPromise = ts.readable.cancel(error1);
      return 1;
    },
    highWaterMark: Infinity
  });
  const writer = ts.writable.getWriter();
  return writer.write('a')
      .then(() => {
        promise_rejects_exactly(t, error1, writer.closed, 'writer.closed should reject');
        return cancelPromise;
      });
}, 'readable cancel() inside size() should work');

promise_test(() => {
  let controller;
  let pipeToPromise;
  const ws = recordingWritableStream();
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  }, undefined, {
    size() {
      if (!pipeToPromise) {
        pipeToPromise = ts.readable.pipeTo(ws);
      }
      return 1;
    },
    highWaterMark: 1
  });
  // Allow promise returned by start() to resolve so that enqueue() will happen synchronously.
  return delay(0).then(() => {
    controller.enqueue('a');
    assert_not_equals(pipeToPromise, undefined);

    // Some pipeTo() implementations need an additional chunk enqueued in order for the first one to be processed. See
    // https://github.com/whatwg/streams/issues/794 for background.
    controller.enqueue('a');

    // Give pipeTo() a chance to process the queued chunks.
    return delay(0);
  }).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'a'], 'ws should contain two chunks');
    controller.terminate();
    return pipeToPromise;
  }).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'a', 'close'], 'target should have been closed');
  });
}, 'pipeTo() inside size() should work');

promise_test(() => {
  let controller;
  let readPromise;
  let calls = 0;
  let reader;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  }, undefined, {
    size() {
      // This is triggered by controller.enqueue(). The queue is empty and there are no pending reads. pull() is called
      // synchronously, allowing transform() to proceed asynchronously. This results in a second call to enqueue(),
      // which resolves this pending read() without calling size() again.
      readPromise = reader.read();
      ++calls;
      return 1;
    },
    highWaterMark: 0
  });
  reader = ts.readable.getReader();
  const writer = ts.writable.getWriter();
  let writeResolved = false;
  const writePromise = writer.write('b').then(() => {
    writeResolved = true;
  });
  return flushAsyncEvents().then(() => {
    assert_false(writeResolved);
    controller.enqueue('a');
    assert_equals(calls, 1, 'size() should have been called once');
    return delay(0);
  }).then(() => {
    assert_true(writeResolved);
    assert_equals(calls, 1, 'size() should only be called once');
    return readPromise;
  }).then(({ value, done }) => {
    assert_false(done, 'done should be false');
    // See https://github.com/whatwg/streams/issues/794 for why this chunk is not 'a'.
    assert_equals(value, 'b', 'chunk should have been read');
    assert_equals(calls, 1, 'calls should still be 1');
    return writePromise;
  });
}, 'read() inside of size() should work');

promise_test(() => {
  let writer;
  let writePromise1;
  let calls = 0;
  const ts = new TransformStream({}, undefined, {
    size() {
      ++calls;
      if (calls < 2) {
        writePromise1 = writer.write('a');
      }
      return 1;
    },
    highWaterMark: Infinity
  });
  writer = ts.writable.getWriter();
  // Give pull() a chance to be called.
  return delay(0).then(() => {
    // This write results in a synchronous call to transform(), enqueue(), and size().
    const writePromise2 = writer.write('b');
    assert_equals(calls, 1, 'size() should have been called once');
    return Promise.all([writePromise1, writePromise2, writer.close()]);
  }).then(() => {
    assert_equals(calls, 2, 'size() should have been called twice');
    return readableStreamToArray(ts.readable);
  }).then(array => {
    assert_array_equals(array, ['b', 'a'], 'both chunks should have been enqueued');
    assert_equals(calls, 2, 'calls should still be 2');
  });
}, 'writer.write() inside size() should work');

promise_test(() => {
  let controller;
  let writer;
  let writePromise;
  let calls = 0;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  }, undefined, {
    size() {
      ++calls;
      if (calls < 2) {
        writePromise = writer.write('a');
      }
      return 1;
    },
    highWaterMark: Infinity
  });
  writer = ts.writable.getWriter();
  // Give pull() a chance to be called.
  return delay(0).then(() => {
    // This enqueue results in synchronous calls to size(), write(), transform() and enqueue().
    controller.enqueue('b');
    assert_equals(calls, 2, 'size() should have been called twice');
    return Promise.all([writePromise, writer.close()]);
  }).then(() => {
    return readableStreamToArray(ts.readable);
  }).then(array => {
    // Because one call to enqueue() is nested inside the other, they finish in the opposite order that they were
    // called, so the chunks end up reverse order.
    assert_array_equals(array, ['a', 'b'], 'both chunks should have been enqueued');
    assert_equals(calls, 2, 'calls should still be 2');
  });
}, 'synchronous writer.write() inside size() should work');

promise_test(() => {
  let writer;
  let closePromise;
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  }, undefined, {
    size() {
      closePromise = writer.close();
      return 1;
    },
    highWaterMark: 1
  });
  writer = ts.writable.getWriter();
  const reader = ts.readable.getReader();
  // Wait for the promise returned by start() to be resolved so that the call to close() will result in a synchronous
  // call to TransformStreamDefaultSink.
  return delay(0).then(() => {
    controller.enqueue('a');
    return reader.read();
  }).then(({ value, done }) => {
    assert_false(done, 'done should be false');
    assert_equals(value, 'a', 'value should be correct');
    return reader.read();
  }).then(({ done }) => {
    assert_true(done, 'done should be true');
    return closePromise;
  });
}, 'writer.close() inside size() should work');

promise_test(t => {
  let abortPromise;
  let controller;
  const ts = new TransformStream({
    start(c) {
      controller = c;
    }
  }, undefined, {
    size() {
      abortPromise = ts.writable.abort(error1);
      return 1;
    },
    highWaterMark: 1
  });
  const reader = ts.readable.getReader();
  // Wait for the promise returned by start() to be resolved so that the call to abort() will result in a synchronous
  // call to TransformStreamDefaultSink.
  return delay(0).then(() => {
    controller.enqueue('a');
    return reader.read();
  }).then(({ value, done }) => {
    assert_false(done, 'done should be false');
    assert_equals(value, 'a', 'value should be correct');
    return Promise.all([promise_rejects_exactly(t, error1, reader.read(), 'read() should reject'), abortPromise]);
  });
}, 'writer.abort() inside size() should work');
