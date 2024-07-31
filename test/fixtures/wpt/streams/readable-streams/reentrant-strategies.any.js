// META: global=window,worker,shadowrealm
// META: script=../resources/recording-streams.js
// META: script=../resources/rs-utils.js
// META: script=../resources/test-utils.js
'use strict';

// The size() function of the readable strategy can re-entrantly call back into the ReadableStream implementation. This
// makes it risky to cache state across the call to ReadableStreamDefaultControllerEnqueue. These tests attempt to catch
// such errors. They are separated from the other strategy tests because no real user code should ever do anything like
// this.

const error1 = new Error('error1');
error1.name = 'error1';

promise_test(() => {
  let controller;
  let calls = 0;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  }, {
    size() {
      ++calls;
      if (calls < 2) {
        controller.enqueue('b');
      }
      return 1;
    }
  });
  controller.enqueue('a');
  controller.close();
  return readableStreamToArray(rs)
      .then(array => assert_array_equals(array, ['b', 'a'], 'array should contain two chunks'));
}, 'enqueue() inside size() should work');

promise_test(() => {
  let controller;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  }, {
    size() {
      // The queue is empty.
      controller.close();
      // The state has gone from "readable" to "closed".
      return 1;
      // This chunk will be enqueued, but will be impossible to read because the state is already "closed".
    }
  });
  controller.enqueue('a');
  return readableStreamToArray(rs)
      .then(array => assert_array_equals(array, [], 'array should contain no chunks'));
  // The chunk 'a' is still in rs's queue. It is closed so 'a' cannot be read.
}, 'close() inside size() should not crash');

promise_test(() => {
  let controller;
  let calls = 0;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  }, {
    size() {
      ++calls;
      if (calls === 2) {
        // The queue contains one chunk.
        controller.close();
        // The state is still "readable", but closeRequest is now true.
      }
      return 1;
    }
  });
  controller.enqueue('a');
  controller.enqueue('b');
  return readableStreamToArray(rs)
      .then(array => assert_array_equals(array, ['a', 'b'], 'array should contain two chunks'));
}, 'close request inside size() should work');

promise_test(t => {
  let controller;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  }, {
    size() {
      controller.error(error1);
      return 1;
    }
  });
  controller.enqueue('a');
  return promise_rejects_exactly(t, error1, rs.getReader().read(), 'read() should reject');
}, 'error() inside size() should work');

promise_test(() => {
  let controller;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  }, {
    size() {
      assert_equals(controller.desiredSize, 1, 'desiredSize should be 1');
      return 1;
    },
    highWaterMark: 1
  });
  controller.enqueue('a');
  controller.close();
  return readableStreamToArray(rs)
      .then(array => assert_array_equals(array, ['a'], 'array should contain one chunk'));
}, 'desiredSize inside size() should work');

promise_test(t => {
  let cancelPromise;
  let controller;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    },
    cancel: t.step_func(reason => {
      assert_equals(reason, error1, 'reason should be error1');
      assert_throws_js(TypeError, () => controller.enqueue(), 'enqueue() should throw');
    })
  }, {
    size() {
      cancelPromise = rs.cancel(error1);
      return 1;
    },
    highWaterMark: Infinity
  });
  controller.enqueue('a');
  const reader = rs.getReader();
  return Promise.all([
    reader.closed,
    cancelPromise
  ]);
}, 'cancel() inside size() should work');

promise_test(() => {
  let controller;
  let pipeToPromise;
  const ws = recordingWritableStream();
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  }, {
    size() {
      if (!pipeToPromise) {
        pipeToPromise = rs.pipeTo(ws);
      }
      return 1;
    },
    highWaterMark: 1
  });
  controller.enqueue('a');
  assert_not_equals(pipeToPromise, undefined);

  // Some pipeTo() implementations need an additional chunk enqueued in order for the first one to be processed. See
  // https://github.com/whatwg/streams/issues/794 for background.
  controller.enqueue('a');

  // Give pipeTo() a chance to process the queued chunks.
  return delay(0).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'a'], 'ws should contain two chunks');
    controller.close();
    return pipeToPromise;
  }).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'a', 'close'], 'target should have been closed');
  });
}, 'pipeTo() inside size() should behave as expected');

promise_test(() => {
  let controller;
  let readPromise;
  let calls = 0;
  let readResolved = false;
  let reader;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  }, {
    size() {
      // This is triggered by controller.enqueue(). The queue is empty and there are no pending reads. This read is
      // added to the list of pending reads.
      readPromise = reader.read();
      ++calls;
      return 1;
    },
    highWaterMark: 0
  });
  reader = rs.getReader();
  controller.enqueue('a');
  readPromise.then(() => {
    readResolved = true;
  });
  return flushAsyncEvents().then(() => {
    assert_false(readResolved);
    controller.enqueue('b');
    assert_equals(calls, 1, 'size() should have been called once');
    return delay(0);
  }).then(() => {
    assert_true(readResolved);
    assert_equals(calls, 1, 'size() should only be called once');
    return readPromise;
  }).then(({ value, done }) => {
    assert_false(done, 'done should be false');
    // See https://github.com/whatwg/streams/issues/794 for why this chunk is not 'a'.
    assert_equals(value, 'b', 'chunk should have been read');
    assert_equals(calls, 1, 'calls should still be 1');
    return reader.read();
  }).then(({ value, done }) => {
    assert_false(done, 'done should be false again');
    assert_equals(value, 'a', 'chunk a should come after b');
  });
}, 'read() inside of size() should behave as expected');

promise_test(() => {
  let controller;
  let reader;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  }, {
    size() {
      reader = rs.getReader();
      return 1;
    }
  });
  controller.enqueue('a');
  return reader.read().then(({ value, done }) => {
    assert_false(done, 'done should be false');
    assert_equals(value, 'a', 'value should be a');
  });
}, 'getReader() inside size() should work');

promise_test(() => {
  let controller;
  let branch1;
  let branch2;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  }, {
    size() {
      [branch1, branch2] = rs.tee();
      return 1;
    }
  });
  controller.enqueue('a');
  assert_true(rs.locked, 'rs should be locked');
  controller.close();
  return Promise.all([
    readableStreamToArray(branch1).then(array => assert_array_equals(array, ['a'], 'branch1 should have one chunk')),
    readableStreamToArray(branch2).then(array => assert_array_equals(array, ['a'], 'branch2 should have one chunk'))
  ]);
}, 'tee() inside size() should work');
