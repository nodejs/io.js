// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
'use strict';

const error1 = new Error('error1');
error1.name = 'error1';

const error2 = new Error('error2');
error2.name = 'error2';

function writeArrayToStream(array, writableStreamWriter) {
  array.forEach(chunk => writableStreamWriter.write(chunk));
  return writableStreamWriter.close();
}

promise_test(() => {
  let storage;
  const ws = new WritableStream({
    start() {
      storage = [];
    },

    write(chunk) {
      return delay(0).then(() => storage.push(chunk));
    },

    close() {
      return delay(0);
    }
  });

  const writer = ws.getWriter();

  const input = [1, 2, 3, 4, 5];
  return writeArrayToStream(input, writer)
      .then(() => assert_array_equals(storage, input, 'correct data should be relayed to underlying sink'));
}, 'WritableStream should complete asynchronous writes before close resolves');

promise_test(() => {
  const ws = recordingWritableStream();

  const writer = ws.getWriter();

  const input = [1, 2, 3, 4, 5];
  return writeArrayToStream(input, writer)
      .then(() => assert_array_equals(ws.events, ['write', 1, 'write', 2, 'write', 3, 'write', 4, 'write', 5, 'close'],
                                      'correct data should be relayed to underlying sink'));
}, 'WritableStream should complete synchronous writes before close resolves');

promise_test(() => {
  const ws = new WritableStream({
    write() {
      return 'Hello';
    }
  });

  const writer = ws.getWriter();

  const writePromise = writer.write('a');
  return writePromise
      .then(value => assert_equals(value, undefined, 'fulfillment value must be undefined'));
}, 'fulfillment value of ws.write() call should be undefined even if the underlying sink returns a non-undefined ' +
    'value');

promise_test(() => {
  let resolveSinkWritePromise;
  const ws = new WritableStream({
    write() {
      return new Promise(resolve => {
        resolveSinkWritePromise = resolve;
      });
    }
  });

  const writer = ws.getWriter();

  assert_equals(writer.desiredSize, 1, 'desiredSize should be 1');

  return writer.ready.then(() => {
    const writePromise = writer.write('a');
    let writePromiseResolved = false;
    assert_not_equals(resolveSinkWritePromise, undefined, 'resolveSinkWritePromise should not be undefined');

    assert_equals(writer.desiredSize, 0, 'desiredSize should be 0 after writer.write()');

    return Promise.all([
      writePromise.then(value => {
        writePromiseResolved = true;
        assert_equals(resolveSinkWritePromise, undefined, 'sinkWritePromise should be fulfilled before writePromise');

        assert_equals(value, undefined, 'writePromise should be fulfilled with undefined');
      }),
      writer.ready.then(value => {
        assert_equals(resolveSinkWritePromise, undefined, 'sinkWritePromise should be fulfilled before writer.ready');
        assert_true(writePromiseResolved, 'writePromise should be fulfilled before writer.ready');

        assert_equals(writer.desiredSize, 1, 'desiredSize should be 1 again');

        assert_equals(value, undefined, 'writePromise should be fulfilled with undefined');
      }),
      flushAsyncEvents().then(() => {
        resolveSinkWritePromise();
        resolveSinkWritePromise = undefined;
      })
    ]);
  });
}, 'WritableStream should transition to waiting until write is acknowledged');

promise_test(t => {
  let sinkWritePromiseRejectors = [];
  const ws = new WritableStream({
    write() {
      const sinkWritePromise = new Promise((r, reject) => sinkWritePromiseRejectors.push(reject));
      return sinkWritePromise;
    }
  });

  const writer = ws.getWriter();

  assert_equals(writer.desiredSize, 1, 'desiredSize should be 1');

  return writer.ready.then(() => {
    const writePromise = writer.write('a');
    assert_equals(sinkWritePromiseRejectors.length, 1, 'there should be 1 rejector');
    assert_equals(writer.desiredSize, 0, 'desiredSize should be 0');

    const writePromise2 = writer.write('b');
    assert_equals(sinkWritePromiseRejectors.length, 1, 'there should be still 1 rejector');
    assert_equals(writer.desiredSize, -1, 'desiredSize should be -1');

    const closedPromise = writer.close();

    assert_equals(writer.desiredSize, -1, 'desiredSize should still be -1');

    return Promise.all([
      promise_rejects_exactly(t, error1, closedPromise,
                              'closedPromise should reject with the error returned from the sink\'s write method')
          .then(() => assert_equals(sinkWritePromiseRejectors.length, 0,
                                    'sinkWritePromise should reject before closedPromise')),
      promise_rejects_exactly(t, error1, writePromise,
                              'writePromise should reject with the error returned from the sink\'s write method')
          .then(() => assert_equals(sinkWritePromiseRejectors.length, 0,
                                    'sinkWritePromise should reject before writePromise')),
      promise_rejects_exactly(t, error1, writePromise2,
                              'writePromise2 should reject with the error returned from the sink\'s write method')
          .then(() => assert_equals(sinkWritePromiseRejectors.length, 0,
                                    'sinkWritePromise should reject before writePromise2')),
      flushAsyncEvents().then(() => {
        sinkWritePromiseRejectors[0](error1);
        sinkWritePromiseRejectors = [];
      })
    ]);
  });
}, 'when write returns a rejected promise, queued writes and close should be cleared');

promise_test(t => {
  const ws = new WritableStream({
    write() {
      throw error1;
    }
  });

  const writer = ws.getWriter();

  return promise_rejects_exactly(t, error1, writer.write('a'),
                                 'write() should reject with the error returned from the sink\'s write method')
      .then(() => promise_rejects_js(t, TypeError, writer.close(), 'close() should be rejected'));
}, 'when sink\'s write throws an error, the stream should become errored and the promise should reject');

promise_test(t => {
  const ws = new WritableStream({
    write(chunk, controller) {
      controller.error(error1);
      throw error2;
    }
  });

  const writer = ws.getWriter();

  return promise_rejects_exactly(t, error2, writer.write('a'),
                                 'write() should reject with the error returned from the sink\'s write method ')
  .then(() => {
    return Promise.all([
      promise_rejects_exactly(t, error1, writer.ready,
                              'writer.ready must reject with the error passed to the controller'),
      promise_rejects_exactly(t, error1, writer.closed,
                              'writer.closed must reject with the error passed to the controller')
    ]);
  });
}, 'writer.write(), ready and closed reject with the error passed to controller.error() made before sink.write' +
    ' rejection');

promise_test(() => {
  const numberOfWrites = 1000;

  let resolveFirstWritePromise;
  let writeCount = 0;
  const ws = new WritableStream({
    write() {
      ++writeCount;
      if (!resolveFirstWritePromise) {
        return new Promise(resolve => {
          resolveFirstWritePromise = resolve;
        });
      }
      return Promise.resolve();
    }
  });

  const writer = ws.getWriter();
  return writer.ready.then(() => {
    for (let i = 1; i < numberOfWrites; ++i) {
      writer.write('a');
    }
    const writePromise = writer.write('a');

    assert_equals(writeCount, 1, 'should have called sink\'s write once');

    resolveFirstWritePromise();

    return writePromise
        .then(() =>
        assert_equals(writeCount, numberOfWrites, `should have called sink's write ${numberOfWrites} times`));
  });
}, 'a large queue of writes should be processed completely');

promise_test(() => {
  const stream = recordingWritableStream();
  const w = stream.getWriter();
  const WritableStreamDefaultWriter = w.constructor;
  w.releaseLock();
  const writer = new WritableStreamDefaultWriter(stream);
  return writer.ready.then(() => {
    writer.write('a');
    assert_array_equals(stream.events, ['write', 'a'], 'write() should be passed to sink');
  });
}, 'WritableStreamDefaultWriter should work when manually constructed');

promise_test(() => {
  let thenCalled = false;
  const ws = new WritableStream({
    write() {
      return {
        then(onFulfilled) {
          thenCalled = true;
          onFulfilled();
        }
      };
    }
  });
  return ws.getWriter().write('a').then(() => assert_true(thenCalled, 'thenCalled should be true'));
}, 'returning a thenable from write() should work');

promise_test(() => {
  const stream = new WritableStream();
  const writer = stream.getWriter();
  const WritableStreamDefaultWriter = writer.constructor;
  assert_throws_js(TypeError, () => new WritableStreamDefaultWriter(stream),
                   'should not be able to construct on locked stream');
  // If stream.[[writer]] no longer points to |writer| then the closed Promise
  // won't work properly.
  return Promise.all([writer.close(), writer.closed]);
}, 'failing DefaultWriter constructor should not release an existing writer');

promise_test(t => {
  const ws = new WritableStream({
    start() {
      return Promise.reject(error1);
    }
  }, { highWaterMark: 0 });
  const writer = ws.getWriter();
  return Promise.all([
    promise_rejects_exactly(t, error1, writer.ready, 'ready should be rejected'),
    promise_rejects_exactly(t, error1, writer.write(), 'write() should be rejected')
  ]);
}, 'write() on a stream with HWM 0 should not cause the ready Promise to resolve');

promise_test(t => {
  const ws = new WritableStream();
  const writer = ws.getWriter();
  writer.releaseLock();
  return promise_rejects_js(t, TypeError, writer.write(), 'write should reject');
}, 'writing to a released writer should reject the returned promise');
