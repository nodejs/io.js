// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/rs-utils.js
// META: script=../resources/recording-streams.js
'use strict';

const error1 = new Error('error1!');
error1.name = 'error1';

promise_test(t => {

  const rs = recordingReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.enqueue('b');
      controller.close();
    }
  });

  const ws = recordingWritableStream(undefined, new CountQueuingStrategy({ highWaterMark: 0 }));

  const pipePromise = rs.pipeTo(ws, { preventCancel: true });

  // Wait and make sure it doesn't do any reading.
  return flushAsyncEvents().then(() => {
    ws.controller.error(error1);
  })
  .then(() => promise_rejects_exactly(t, error1, pipePromise, 'pipeTo must reject with the same error'))
  .then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, []);
  })
  .then(() => readableStreamToArray(rs))
  .then(chunksNotPreviouslyRead => {
    assert_array_equals(chunksNotPreviouslyRead, ['a', 'b']);
  });

}, 'Piping from a non-empty ReadableStream into a WritableStream that does not desire chunks');

promise_test(() => {

  const rs = recordingReadableStream({
    start(controller) {
      controller.enqueue('b');
      controller.close();
    }
  });

  let resolveWritePromise;
  const ws = recordingWritableStream({
    write() {
      if (!resolveWritePromise) {
        // first write
        return new Promise(resolve => {
          resolveWritePromise = resolve;
        });
      }
      return undefined;
    }
  });

  const writer = ws.getWriter();
  const firstWritePromise = writer.write('a');
  assert_equals(writer.desiredSize, 0, 'after writing the writer\'s desiredSize must be 0');
  writer.releaseLock();

  // firstWritePromise won't settle until we call resolveWritePromise.

  const pipePromise = rs.pipeTo(ws);

  return flushAsyncEvents().then(() => resolveWritePromise())
    .then(() => Promise.all([firstWritePromise, pipePromise]))
    .then(() => {
      assert_array_equals(rs.eventsWithoutPulls, []);
      assert_array_equals(ws.events, ['write', 'a', 'write', 'b', 'close']);
    });

}, 'Piping from a non-empty ReadableStream into a WritableStream that does not desire chunks, but then does');

promise_test(() => {

  const rs = recordingReadableStream();

  let resolveWritePromise;
  const ws = recordingWritableStream({
    write() {
      if (!resolveWritePromise) {
        // first write
        return new Promise(resolve => {
          resolveWritePromise = resolve;
        });
      }
      return undefined;
    }
  });

  const writer = ws.getWriter();
  writer.write('a');

  return flushAsyncEvents().then(() => {
    assert_array_equals(ws.events, ['write', 'a']);
    assert_equals(writer.desiredSize, 0, 'after writing the writer\'s desiredSize must be 0');
    writer.releaseLock();

    const pipePromise = rs.pipeTo(ws);

    rs.controller.enqueue('b');
    resolveWritePromise();
    rs.controller.close();

    return pipePromise.then(() => {
      assert_array_equals(rs.eventsWithoutPulls, []);
      assert_array_equals(ws.events, ['write', 'a', 'write', 'b', 'close']);
    });
  });

}, 'Piping from an empty ReadableStream into a WritableStream that does not desire chunks, but then the readable ' +
   'stream becomes non-empty and the writable stream starts desiring chunks');

promise_test(() => {
  const unreadChunks = ['b', 'c', 'd'];

  const rs = recordingReadableStream({
    pull(controller) {
      controller.enqueue(unreadChunks.shift());
      if (unreadChunks.length === 0) {
        controller.close();
      }
    }
  }, new CountQueuingStrategy({ highWaterMark: 0 }));

  let resolveWritePromise;
  const ws = recordingWritableStream({
    write() {
      if (!resolveWritePromise) {
        // first write
        return new Promise(resolve => {
          resolveWritePromise = resolve;
        });
      }
      return undefined;
    }
  }, new CountQueuingStrategy({ highWaterMark: 3 }));

  const writer = ws.getWriter();
  const firstWritePromise = writer.write('a');
  assert_equals(writer.desiredSize, 2, 'after writing the writer\'s desiredSize must be 2');
  writer.releaseLock();

  // firstWritePromise won't settle until we call resolveWritePromise.

  const pipePromise = rs.pipeTo(ws);

  return flushAsyncEvents().then(() => {
    assert_array_equals(ws.events, ['write', 'a']);
    assert_equals(unreadChunks.length, 1, 'chunks should continue to be enqueued until the HWM is reached');
  }).then(() => resolveWritePromise())
    .then(() => Promise.all([firstWritePromise, pipePromise]))
    .then(() => {
      assert_array_equals(rs.events, ['pull', 'pull', 'pull']);
      assert_array_equals(ws.events, ['write', 'a', 'write', 'b','write', 'c','write', 'd', 'close']);
    });

}, 'Piping from a ReadableStream to a WritableStream that desires more chunks before finishing with previous ones');

class StepTracker {
  constructor() {
    this.waiters = [];
    this.wakers = [];
  }

  // Returns promise which resolves when step `n` is reached. Also schedules step n + 1 to happen shortly after the
  // promise is resolved.
  waitThenAdvance(n) {
    if (this.waiters[n] === undefined) {
      this.waiters[n] = new Promise(resolve => {
        this.wakers[n] = resolve;
      });
      this.waiters[n]
          .then(() => flushAsyncEvents())
          .then(() => {
            if (this.wakers[n + 1] !== undefined) {
              this.wakers[n + 1]();
            }
          });
    }
    if (n == 0) {
      this.wakers[0]();
    }
    return this.waiters[n];
  }
}

promise_test(() => {
  const steps = new StepTracker();
  const desiredSizes = [];
  const rs = recordingReadableStream({
    start(controller) {
      steps.waitThenAdvance(1).then(() => enqueue('a'));
      steps.waitThenAdvance(3).then(() => enqueue('b'));
      steps.waitThenAdvance(5).then(() => enqueue('c'));
      steps.waitThenAdvance(7).then(() => enqueue('d'));
      steps.waitThenAdvance(11).then(() => controller.close());

      function enqueue(chunk) {
        controller.enqueue(chunk);
        desiredSizes.push(controller.desiredSize);
      }
    }
  });

  const chunksFinishedWriting = [];
  const writableStartPromise = Promise.resolve();
  let writeCalled = false;
  const ws = recordingWritableStream({
    start() {
      return writableStartPromise;
    },
    write(chunk) {
      const waitForStep = writeCalled ? 12 : 9;
      writeCalled = true;
      return steps.waitThenAdvance(waitForStep).then(() => {
        chunksFinishedWriting.push(chunk);
      });
    }
  });

  return writableStartPromise.then(() => {
    const pipePromise = rs.pipeTo(ws);
    steps.waitThenAdvance(0);

    return Promise.all([
      steps.waitThenAdvance(2).then(() => {
        assert_array_equals(chunksFinishedWriting, [], 'at step 2, zero chunks must have finished writing');
        assert_array_equals(ws.events, ['write', 'a'], 'at step 2, one chunk must have been written');

        // When 'a' (the very first chunk) was enqueued, it was immediately used to fulfill the outstanding read request
        // promise, leaving the queue empty.
        assert_array_equals(desiredSizes, [1],
                            'at step 2, the desiredSize at the last enqueue (step 1) must have been 1');
        assert_equals(rs.controller.desiredSize, 1, 'at step 2, the current desiredSize must be 1');
      }),

      steps.waitThenAdvance(4).then(() => {
        assert_array_equals(chunksFinishedWriting, [], 'at step 4, zero chunks must have finished writing');
        assert_array_equals(ws.events, ['write', 'a'], 'at step 4, one chunk must have been written');

        // When 'b' was enqueued at step 3, the queue was also empty, since immediately after enqueuing 'a' at
        // step 1, it was dequeued in order to fulfill the read() call that was made at step 0. Thus the queue
        // had size 1 (thus desiredSize of 0).
        assert_array_equals(desiredSizes, [1, 0],
                            'at step 4, the desiredSize at the last enqueue (step 3) must have been 0');
        assert_equals(rs.controller.desiredSize, 0, 'at step 4, the current desiredSize must be 0');
      }),

      steps.waitThenAdvance(6).then(() => {
        assert_array_equals(chunksFinishedWriting, [], 'at step 6, zero chunks must have finished writing');
        assert_array_equals(ws.events, ['write', 'a'], 'at step 6, one chunk must have been written');

        // When 'c' was enqueued at step 5, the queue was not empty; it had 'b' in it, since 'b' will not be read until
        // the first write completes at step 9. Thus, the queue size is 2 after enqueuing 'c', giving a desiredSize of
        // -1.
        assert_array_equals(desiredSizes, [1, 0, -1],
                            'at step 6, the desiredSize at the last enqueue (step 5) must have been -1');
        assert_equals(rs.controller.desiredSize, -1, 'at step 6, the current desiredSize must be -1');
      }),

      steps.waitThenAdvance(8).then(() => {
        assert_array_equals(chunksFinishedWriting, [], 'at step 8, zero chunks must have finished writing');
        assert_array_equals(ws.events, ['write', 'a'], 'at step 8, one chunk must have been written');

        // When 'd' was enqueued at step 7, the situation is the same as before, leading to a queue containing 'b', 'c',
        // and 'd'.
        assert_array_equals(desiredSizes, [1, 0, -1, -2],
                            'at step 8, the desiredSize at the last enqueue (step 7) must have been -2');
        assert_equals(rs.controller.desiredSize, -2, 'at step 8, the current desiredSize must be -2');
      }),

      steps.waitThenAdvance(10).then(() => {
        assert_array_equals(chunksFinishedWriting, ['a'], 'at step 10, one chunk must have finished writing');
        assert_array_equals(ws.events, ['write', 'a', 'write', 'b'],
                            'at step 10, two chunks must have been written');

        assert_equals(rs.controller.desiredSize, -1, 'at step 10, the current desiredSize must be -1');
      }),

      pipePromise.then(() => {
        assert_array_equals(desiredSizes, [1, 0, -1, -2], 'backpressure must have been exerted at the source');
        assert_array_equals(chunksFinishedWriting, ['a', 'b', 'c', 'd'], 'all chunks finished writing');

        assert_array_equals(rs.eventsWithoutPulls, [], 'nothing unexpected should happen to the ReadableStream');
        assert_array_equals(ws.events, ['write', 'a', 'write', 'b', 'write', 'c', 'write', 'd', 'close'],
                            'all chunks were written (and the WritableStream closed)');
      })
    ]);
  });
}, 'Piping to a WritableStream that does not consume the writes fast enough exerts backpressure on the ReadableStream');
