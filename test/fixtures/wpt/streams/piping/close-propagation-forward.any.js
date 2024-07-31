// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
'use strict';

const error1 = new Error('error1!');
error1.name = 'error1';

promise_test(() => {

  const rs = recordingReadableStream({
    start(controller) {
      controller.close();
    }
  });

  const ws = recordingWritableStream();

  return rs.pipeTo(ws).then(value => {
    assert_equals(value, undefined, 'the promise must fulfill with undefined');
  })
  .then(() => {
    assert_array_equals(rs.events, []);
    assert_array_equals(ws.events, ['close']);

    return Promise.all([
      rs.getReader().closed,
      ws.getWriter().closed
    ]);
  });

}, 'Closing must be propagated forward: starts closed; preventClose omitted; fulfilled close promise');

promise_test(t => {

  const rs = recordingReadableStream({
    start(controller) {
      controller.close();
    }
  });

  const ws = recordingWritableStream({
    close() {
      throw error1;
    }
  });

  return promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the same error').then(() => {
    assert_array_equals(rs.events, []);
    assert_array_equals(ws.events, ['close']);

    return Promise.all([
      rs.getReader().closed,
      promise_rejects_exactly(t, error1, ws.getWriter().closed)
    ]);
  });

}, 'Closing must be propagated forward: starts closed; preventClose omitted; rejected close promise');

for (const falsy of [undefined, null, false, +0, -0, NaN, '']) {
  const stringVersion = Object.is(falsy, -0) ? '-0' : String(falsy);

  promise_test(() => {

    const rs = recordingReadableStream({
      start(controller) {
        controller.close();
      }
    });

    const ws = recordingWritableStream();

    return rs.pipeTo(ws, { preventClose: falsy }).then(value => {
      assert_equals(value, undefined, 'the promise must fulfill with undefined');
    })
    .then(() => {
      assert_array_equals(rs.events, []);
      assert_array_equals(ws.events, ['close']);

      return Promise.all([
        rs.getReader().closed,
        ws.getWriter().closed
      ]);
    });

  }, `Closing must be propagated forward: starts closed; preventClose = ${stringVersion} (falsy); fulfilled close ` +
     `promise`);
}

for (const truthy of [true, 'a', 1, Symbol(), { }]) {
  promise_test(() => {

    const rs = recordingReadableStream({
      start(controller) {
        controller.close();
      }
    });

    const ws = recordingWritableStream();

    return rs.pipeTo(ws, { preventClose: truthy }).then(value => {
      assert_equals(value, undefined, 'the promise must fulfill with undefined');
    })
    .then(() => {
      assert_array_equals(rs.events, []);
      assert_array_equals(ws.events, []);

      return rs.getReader().closed;
    });

  }, `Closing must be propagated forward: starts closed; preventClose = ${String(truthy)} (truthy)`);
}

promise_test(() => {

  const rs = recordingReadableStream({
    start(controller) {
      controller.close();
    }
  });

  const ws = recordingWritableStream();

  return rs.pipeTo(ws, { preventClose: true, preventAbort: true }).then(value => {
    assert_equals(value, undefined, 'the promise must fulfill with undefined');
  })
  .then(() => {
    assert_array_equals(rs.events, []);
    assert_array_equals(ws.events, []);

    return rs.getReader().closed;
  });

}, 'Closing must be propagated forward: starts closed; preventClose = true, preventAbort = true');

promise_test(() => {

  const rs = recordingReadableStream({
    start(controller) {
      controller.close();
    }
  });

  const ws = recordingWritableStream();

  return rs.pipeTo(ws, { preventClose: true, preventAbort: true, preventCancel: true }).then(value => {
    assert_equals(value, undefined, 'the promise must fulfill with undefined');
  })
  .then(() => {
    assert_array_equals(rs.events, []);
    assert_array_equals(ws.events, []);

    return rs.getReader().closed;
  });

}, 'Closing must be propagated forward: starts closed; preventClose = true, preventAbort = true, preventCancel = true');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream();

  const pipePromise = rs.pipeTo(ws);

  t.step_timeout(() => rs.controller.close());

  return pipePromise.then(value => {
    assert_equals(value, undefined, 'the promise must fulfill with undefined');
  })
  .then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['close']);

    return Promise.all([
      rs.getReader().closed,
      ws.getWriter().closed
    ]);
  });

}, 'Closing must be propagated forward: becomes closed asynchronously; preventClose omitted; fulfilled close promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream({
    close() {
      throw error1;
    }
  });

  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the same error');

  t.step_timeout(() => rs.controller.close());

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['close']);

    return Promise.all([
      rs.getReader().closed,
      promise_rejects_exactly(t, error1, ws.getWriter().closed)
    ]);
  });

}, 'Closing must be propagated forward: becomes closed asynchronously; preventClose omitted; rejected close promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream();

  const pipePromise = rs.pipeTo(ws, { preventClose: true });

  t.step_timeout(() => rs.controller.close());

  return pipePromise.then(value => {
    assert_equals(value, undefined, 'the promise must fulfill with undefined');
  })
  .then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, []);

    return rs.getReader().closed;
  });

}, 'Closing must be propagated forward: becomes closed asynchronously; preventClose = true');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream(undefined, new CountQueuingStrategy({ highWaterMark: 0 }));

  const pipePromise = rs.pipeTo(ws);

  t.step_timeout(() => rs.controller.close());

  return pipePromise.then(value => {
    assert_equals(value, undefined, 'the promise must fulfill with undefined');
  })
  .then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['close']);

    return Promise.all([
      rs.getReader().closed,
      ws.getWriter().closed
    ]);
  });

}, 'Closing must be propagated forward: becomes closed asynchronously; dest never desires chunks; ' +
   'preventClose omitted; fulfilled close promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream({
    close() {
      throw error1;
    }
  }, new CountQueuingStrategy({ highWaterMark: 0 }));

  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the same error');

  t.step_timeout(() => rs.controller.close());

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['close']);

    return Promise.all([
      rs.getReader().closed,
      promise_rejects_exactly(t, error1, ws.getWriter().closed)
    ]);
  });

}, 'Closing must be propagated forward: becomes closed asynchronously; dest never desires chunks; ' +
   'preventClose omitted; rejected close promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream(undefined, new CountQueuingStrategy({ highWaterMark: 0 }));

  const pipePromise = rs.pipeTo(ws, { preventClose: true });

  t.step_timeout(() => rs.controller.close());

  return pipePromise.then(value => {
    assert_equals(value, undefined, 'the promise must fulfill with undefined');
  })
  .then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, []);

    return rs.getReader().closed;
  });

}, 'Closing must be propagated forward: becomes closed asynchronously; dest never desires chunks; ' +
   'preventClose = true');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream();

  const pipePromise = rs.pipeTo(ws);

  t.step_timeout(() => {
    rs.controller.enqueue('Hello');
    t.step_timeout(() => rs.controller.close());
  }, 10);

  return pipePromise.then(value => {
    assert_equals(value, undefined, 'the promise must fulfill with undefined');
  })
  .then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['write', 'Hello', 'close']);

    return Promise.all([
      rs.getReader().closed,
      ws.getWriter().closed
    ]);
  });

}, 'Closing must be propagated forward: becomes closed after one chunk; preventClose omitted; fulfilled close promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream({
    close() {
      throw error1;
    }
  });

  const pipePromise = promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the same error');

  t.step_timeout(() => {
    rs.controller.enqueue('Hello');
    t.step_timeout(() => rs.controller.close());
  }, 10);

  return pipePromise.then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['write', 'Hello', 'close']);

    return Promise.all([
      rs.getReader().closed,
      promise_rejects_exactly(t, error1, ws.getWriter().closed)
    ]);
  });

}, 'Closing must be propagated forward: becomes closed after one chunk; preventClose omitted; rejected close promise');

promise_test(t => {

  const rs = recordingReadableStream();

  const ws = recordingWritableStream();

  const pipePromise = rs.pipeTo(ws, { preventClose: true });

  t.step_timeout(() => {
    rs.controller.enqueue('Hello');
    t.step_timeout(() => rs.controller.close());
  }, 10);

  return pipePromise.then(value => {
    assert_equals(value, undefined, 'the promise must fulfill with undefined');
  })
  .then(() => {
    assert_array_equals(rs.eventsWithoutPulls, []);
    assert_array_equals(ws.events, ['write', 'Hello']);

    return rs.getReader().closed;
  });

}, 'Closing must be propagated forward: becomes closed after one chunk; preventClose = true');

promise_test(() => {

  const rs = recordingReadableStream();

  let resolveWritePromise;
  const ws = recordingWritableStream({
    write() {
      return new Promise(resolve => {
        resolveWritePromise = resolve;
      });
    }
  });

  let pipeComplete = false;
  const pipePromise = rs.pipeTo(ws).then(() => {
    pipeComplete = true;
  });

  rs.controller.enqueue('a');
  rs.controller.close();

  // Flush async events and verify that no shutdown occurs.
  return flushAsyncEvents().then(() => {
    assert_array_equals(ws.events, ['write', 'a']); // no 'close'
    assert_equals(pipeComplete, false, 'the pipe must not be complete');

    resolveWritePromise();

    return pipePromise.then(() => {
      assert_array_equals(ws.events, ['write', 'a', 'close']);
    });
  });

}, 'Closing must be propagated forward: shutdown must not occur until the final write completes');

promise_test(() => {

  const rs = recordingReadableStream();

  let resolveWritePromise;
  const ws = recordingWritableStream({
    write() {
      return new Promise(resolve => {
        resolveWritePromise = resolve;
      });
    }
  });

  let pipeComplete = false;
  const pipePromise = rs.pipeTo(ws, { preventClose: true }).then(() => {
    pipeComplete = true;
  });

  rs.controller.enqueue('a');
  rs.controller.close();

  // Flush async events and verify that no shutdown occurs.
  return flushAsyncEvents().then(() => {
    assert_array_equals(ws.events, ['write', 'a'],
      'the chunk must have been written, but close must not have happened');
    assert_equals(pipeComplete, false, 'the pipe must not be complete');

    resolveWritePromise();

    return pipePromise;
  }).then(() => flushAsyncEvents()).then(() => {
    assert_array_equals(ws.events, ['write', 'a'],
      'the chunk must have been written, but close must not have happened');
  });

}, 'Closing must be propagated forward: shutdown must not occur until the final write completes; preventClose = true');

promise_test(() => {

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
  const pipePromise = rs.pipeTo(ws).then(() => {
    pipeComplete = true;
  });

  rs.controller.enqueue('a');
  rs.controller.enqueue('b');

  return writeCalledPromise.then(() => flushAsyncEvents()).then(() => {
    assert_array_equals(ws.events, ['write', 'a'],
      'the first chunk must have been written, but close must not have happened yet');
    assert_false(pipeComplete, 'the pipe should not complete while the first write is pending');

    rs.controller.close();
    resolveWritePromise();
  }).then(() => flushAsyncEvents()).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'b'],
      'the second chunk must have been written, but close must not have happened yet');
    assert_false(pipeComplete, 'the pipe should not complete while the second write is pending');

    resolveWritePromise();
    return pipePromise;
  }).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'b', 'close'],
      'all chunks must have been written and close must have happened');
  });

}, 'Closing must be propagated forward: shutdown must not occur until the final write completes; becomes closed after first write');

promise_test(() => {

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
  const pipePromise = rs.pipeTo(ws, { preventClose: true }).then(() => {
    pipeComplete = true;
  });

  rs.controller.enqueue('a');
  rs.controller.enqueue('b');

  return writeCalledPromise.then(() => flushAsyncEvents()).then(() => {
    assert_array_equals(ws.events, ['write', 'a'],
      'the first chunk must have been written, but close must not have happened');
    assert_false(pipeComplete, 'the pipe should not complete while the first write is pending');

    rs.controller.close();
    resolveWritePromise();
  }).then(() => flushAsyncEvents()).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'b'],
      'the second chunk must have been written, but close must not have happened');
    assert_false(pipeComplete, 'the pipe should not complete while the second write is pending');

    resolveWritePromise();
    return pipePromise;
  }).then(() => flushAsyncEvents()).then(() => {
    assert_array_equals(ws.events, ['write', 'a', 'write', 'b'],
      'all chunks must have been written, but close must not have happened');
  });

}, 'Closing must be propagated forward: shutdown must not occur until the final write completes; becomes closed after first write; preventClose = true');


promise_test(t => {
  const rs = recordingReadableStream({
    start(c) {
      c.enqueue('a');
      c.enqueue('b');
      c.close();
    }
  });
  let rejectWritePromise;
  const ws = recordingWritableStream({
    write() {
      return new Promise((resolve, reject) => {
        rejectWritePromise = reject;
      });
    }
  }, { highWaterMark: 3 });
  const pipeToPromise = rs.pipeTo(ws);
  return delay(0).then(() => {
    rejectWritePromise(error1);
    return promise_rejects_exactly(t, error1, pipeToPromise, 'pipeTo should reject');
  }).then(() => {
    assert_array_equals(rs.events, []);
    assert_array_equals(ws.events, ['write', 'a']);

    return Promise.all([
      rs.getReader().closed,
      promise_rejects_exactly(t, error1, ws.getWriter().closed, 'ws should be errored')
    ]);
  });
}, 'Closing must be propagated forward: erroring the writable while flushing pending writes should error pipeTo');
