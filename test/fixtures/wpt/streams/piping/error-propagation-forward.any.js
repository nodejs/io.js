// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
'use strict';

const error1 = new Error('error1!');
error1.name = 'error1';

const error2 = new Error('error2!');
error2.name = 'error2';

promise_test(t => {

  const rs = recordingReadableStream({
    start() {
      return Promise.reject(error1);
    }
  });

  const ws = recordingWritableStream();

  return promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the same error')
    .then(() => {
      assert_array_equals(rs.events, []);
      assert_array_equals(ws.events, ['abort', error1]);
    });

}, 'Errors must be propagated forward: starts errored; preventAbort = false; fulfilled abort promise');

promise_test(t => {

  const rs = recordingReadableStream({
    start() {
      return Promise.reject(error1);
    }
  });

  const ws = recordingWritableStream({
    abort() {
      throw error2;
    }
  });

  return promise_rejects_exactly(t, error2, rs.pipeTo(ws), 'pipeTo must reject with the abort error')
    .then(() => {
      assert_array_equals(rs.events, []);
      assert_array_equals(ws.events, ['abort', error1]);
    });

}, 'Errors must be propagated forward: starts errored; preventAbort = false; rejected abort promise');

for (const falsy of [undefined, null, false, +0, -0, NaN, '']) {
  const stringVersion = Object.is(falsy, -0) ? '-0' : String(falsy);

  promise_test(t => {

    const rs = recordingReadableStream({
      start() {
        return Promise.reject(error1);
      }
    });

    const ws = recordingWritableStream();

    return promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: falsy }), 'pipeTo must reject with the same error')
      .then(() => {
        assert_array_equals(rs.events, []);
        assert_array_equals(ws.events, ['abort', error1]);
      });

  }, `Errors must be propagated forward: starts errored; preventAbort = ${stringVersion} (falsy); fulfilled abort ` +
     `promise`);
}

for (const truthy of [true, 'a', 1, Symbol(), { }]) {
  promise_test(t => {

    const rs = recordingReadableStream({
      start() {
        return Promise.reject(error1);
      }
    });

    const ws = recordingWritableStream();

    return promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: truthy }),
      'pipeTo must reject with the same error')
      .then(() => {
        assert_array_equals(rs.events, []);
        assert_array_equals(ws.events, []);
      });

  }, `Errors must be propagated forward: starts errored; preventAbort = ${String(truthy)} (truthy)`);
}


promise_test(t => {

  const rs = recordingReadableStream({
    start() {
      return Promise.reject(error1);
    }
  });

  const ws = recordingWritableStream();

  return promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: true, preventCancel: true }),
    'pipeTo must reject with the same error')
    .then(() => {
      assert_array_equals(rs.events, []);
      assert_array_equals(ws.events, []);
    });

}, 'Errors must be propagated forward: starts errored; preventAbort = true, preventCancel = true');

promise_test(t => {

  const rs = recordingReadableStream({
    start() {
      return Promise.reject(error1);
    }
  });

  const ws = recordingWritableStream();

  return promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: true, preventCancel: true, preventClose: true }),
    'pipeTo must reject with the same error')
    .then(() => {
      assert_array_equals(rs.events, []);
      assert_array_equals(ws.events, []);
    });

}, 'Errors must be propagated forward: starts errored; preventAbort = true, preventCancel = true, preventClose = true');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream();

  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the same error');

  t.step_timeout(() => rs.controller.error(error1), 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['abort', error1]);
  });

}, 'Errors must be propagated forward: becomes errored while empty; preventAbort = false; fulfilled abort promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream({
    abort() {
      throw error2;
    }
  });

  const pipePromise = promise_rejects_exactly(t, error2, rs.pipeTo(ws), 'pipeTo must reject with the abort error');

  t.step_timeout(() => rs.controller.error(error1), 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['abort', error1]);
  });

}, 'Errors must be propagated forward: becomes errored while empty; preventAbort = false; rejected abort promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream();

  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: true }),
                                              'pipeTo must reject with the same error');

  t.step_timeout(() => rs.controller.error(error1), 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, []);
  });

}, 'Errors must be propagated forward: becomes errored while empty; preventAbort = true');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream(undefined, new CountQueuingStrategy({ highWaterMark: 0 }));

  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the same error');

  t.step_timeout(() => rs.controller.error(error1), 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['abort', error1]);
  });

}, 'Errors must be propagated forward: becomes errored while empty; dest never desires chunks; ' +
   'preventAbort = false; fulfilled abort promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream({
    abort() {
      throw error2;
    }
  }, new CountQueuingStrategy({ highWaterMark: 0 }));

  const pipePromise = promise_rejects_exactly(t, error2, rs.pipeTo(ws), 'pipeTo must reject with the abort error');

  t.step_timeout(() => rs.controller.error(error1), 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['abort', error1]);
  });

}, 'Errors must be propagated forward: becomes errored while empty; dest never desires chunks; ' +
   'preventAbort = false; rejected abort promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream(undefined, new CountQueuingStrategy({ highWaterMark: 0 }));

  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: true }),
                                              'pipeTo must reject with the same error');

  t.step_timeout(() => rs.controller.error(error1), 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, []);
  });

}, 'Errors must be propagated forward: becomes errored while empty; dest never desires chunks; ' +
   'preventAbort = true');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream();

  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the same error');

  t.step_timeout(() => {
    rs.controller.enqueue('Hello');
    t.step_timeout(() => rs.controller.error(error1), 10);
  }, 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['write', 'Hello', 'abort', error1]);
  });

}, 'Errors must be propagated forward: becomes errored after one chunk; preventAbort = false; fulfilled abort promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream({
    abort() {
      throw error2;
    }
  });

  const pipePromise = promise_rejects_exactly(t, error2, rs.pipeTo(ws), 'pipeTo must reject with the abort error');

  t.step_timeout(() => {
    rs.controller.enqueue('Hello');
    t.step_timeout(() => rs.controller.error(error1), 10);
  }, 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['write', 'Hello', 'abort', error1]);
  });

}, 'Errors must be propagated forward: becomes errored after one chunk; preventAbort = false; rejected abort promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream();

  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: true }),
                                              'pipeTo must reject with the same error');

  t.step_timeout(() => {
    rs.controller.enqueue('Hello');
    t.step_timeout(() => rs.controller.error(error1), 10);
  }, 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['write', 'Hello']);
  });

}, 'Errors must be propagated forward: becomes errored after one chunk; preventAbort = true');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream(undefined, new CountQueuingStrategy({ highWaterMark: 0 }));

  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the same error');

  t.step_timeout(() => {
    rs.controller.enqueue('Hello');
    t.step_timeout(() => rs.controller.error(error1), 10);
  }, 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['abort', error1]);
  });

}, 'Errors must be propagated forward: becomes errored after one chunk; dest never desires chunks; ' +
   'preventAbort = false; fulfilled abort promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream({
    abort() {
      throw error2;
    }
  }, new CountQueuingStrategy({ highWaterMark: 0 }));

  const pipePromise = promise_rejects_exactly(t, error2, rs.pipeTo(ws), 'pipeTo must reject with the abort error');

  t.step_timeout(() => {
    rs.controller.enqueue('Hello');
    t.step_timeout(() => rs.controller.error(error1), 10);
  }, 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['abort', error1]);
  });

}, 'Errors must be propagated forward: becomes errored after one chunk; dest never desires chunks; ' +
   'preventAbort = false; rejected abort promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream(undefined, new CountQueuingStrategy({ highWaterMark: 0 }));

  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: true }),
                                              'pipeTo must reject with the same error');

  t.step_timeout(() => {
    rs.controller.enqueue('Hello');
    t.step_timeout(() => rs.controller.error(error1), 10);
  }, 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, []);
  });

}, 'Errors must be propagated forward: becomes errored after one chunk; dest never desires chunks; ' +
   'preventAbort = true');

promise_test(t => {

  const rs = recordingReadableStream();

  let resolveWriteCalled;
  const writeCalledPromise = new Promise(resolve => {
    resolveWriteCalled = resolve;
  });

  let resolveWritePromise;
  const ws = recordingWritableStream({
    write() {
      resolveWriteCalled();

      return new Promise(resolve => {
        resolveWritePromise = resolve;
      });
    }
  });

  let pipeComplete = false;
  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws)).then(() => {
    pipeComplete = true;
  });

  rs.controller.enqueue('a');

  return writeCalledPromise.then(() => {
    rs.controller.error(error1);

    // Flush async events and verify that no shutdown occurs.
    return flushAsyncEvents();
  }).then(() => {
    assert_array_equals(ws.events, ['write', 'a']); // no 'abort'
    assert_equals(pipeComplete, false, 'the pipe must not be complete');

    resolveWritePromise();

    return pipePromise.then(() => {
      assert_array_equals(ws.events, ['write', 'a', 'abort', error1]);
    });
  });

}, 'Errors must be propagated forward: shutdown must not occur until the final write completes');

promise_test(t => {

  const rs = recordingReadableStream();

  let resolveWriteCalled;
  const writeCalledPromise = new Promise(resolve => {
    resolveWriteCalled = resolve;
  });

  let resolveWritePromise;
  const ws = recordingWritableStream({
    write() {
      resolveWriteCalled();

      return new Promise(resolve => {
        resolveWritePromise = resolve;
      });
    }
  });

  let pipeComplete = false;
  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: true })).then(() => {
    pipeComplete = true;
  });

  rs.controller.enqueue('a');

  return writeCalledPromise.then(() => {
    rs.controller.error(error1);

    // Flush async events and verify that no shutdown occurs.
    return flushAsyncEvents();
  }).then(() => {
    assert_array_equals(ws.events, ['write', 'a']); // no 'abort'
    assert_equals(pipeComplete, false, 'the pipe must not be complete');

    resolveWritePromise();
    return pipePromise;
  }).then(() => flushAsyncEvents()).then(() => {
    assert_array_equals(ws.events, ['write', 'a']); // no 'abort'
  });

}, 'Errors must be propagated forward: shutdown must not occur until the final write completes; preventAbort = true');

promise_test(t => {

  const rs = recordingReadableStream();

  let resolveWriteCalled;
  const writeCalledPromise = new Promise(resolve => {
    resolveWriteCalled = resolve;
  });

  let resolveWritePromise;
  const ws = recordingWritableStream({
    write() {
      resolveWriteCalled();

      return new Promise(resolve => {
        resolveWritePromise = resolve;
      });
    }
  }, new CountQueuingStrategy({ highWaterMark: 2 }));

  let pipeComplete = false;
  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws)).then(() => {
    pipeComplete = true;
  });

  rs.controller.enqueue('a');
  rs.controller.enqueue('b');

  return writeCalledPromise.then(() => flushAsyncEvents()).then(() => {
    assert_array_equals(ws.events, ['write', 'a'],
      'the first chunk must have been written, but abort must not have happened yet');
    assert_false(pipeComplete, 'the pipe should not complete while the first write is pending');

    rs.controller.error(error1);
    resolveWritePromise();
    return flushAsyncEvents();
  }).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'b'],
      'the second chunk must have been written, but abort must not have happened yet');
    assert_false(pipeComplete, 'the pipe should not complete while the second write is pending');

    resolveWritePromise();
    return pipePromise;
  }).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'b', 'abort', error1],
      'all chunks must have been written and abort must have happened');
  });

}, 'Errors must be propagated forward: shutdown must not occur until the final write completes; becomes errored after first write');

promise_test(t => {

  const rs = recordingReadableStream();

  let resolveWriteCalled;
  const writeCalledPromise = new Promise(resolve => {
    resolveWriteCalled = resolve;
  });

  let resolveWritePromise;
  const ws = recordingWritableStream({
    write() {
      resolveWriteCalled();

      return new Promise(resolve => {
        resolveWritePromise = resolve;
      });
    }
  }, new CountQueuingStrategy({ highWaterMark: 2 }));

  let pipeComplete = false;
  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: true })).then(() => {
    pipeComplete = true;
  });

  rs.controller.enqueue('a');
  rs.controller.enqueue('b');

  return writeCalledPromise.then(() => flushAsyncEvents()).then(() => {
    assert_array_equals(ws.events, ['write', 'a'],
      'the first chunk must have been written, but abort must not have happened');
    assert_false(pipeComplete, 'the pipe should not complete while the first write is pending');

    rs.controller.error(error1);
    resolveWritePromise();
  }).then(() => flushAsyncEvents()).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'b'],
      'the second chunk must have been written, but abort must not have happened');
    assert_false(pipeComplete, 'the pipe should not complete while the second write is pending');

    resolveWritePromise();
    return pipePromise;
  }).then(() => flushAsyncEvents()).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'b'],
      'all chunks must have been written, but abort must not have happened');
  });

}, 'Errors must be propagated forward: shutdown must not occur until the final write completes; becomes errored after first write; preventAbort = true');
