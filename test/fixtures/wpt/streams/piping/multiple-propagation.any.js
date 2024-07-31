// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
'use strict';

const error1 = new Error('error1!');
error1.name = 'error1';

const error2 = new Error('error2!');
error2.name = 'error2';

function createErroredWritableStream(t) {
  return Promise.resolve().then(() => {
    const ws = recordingWritableStream({
      start(c) {
        c.error(error2);
      }
    });

    const writer = ws.getWriter();
    return promise_rejects_exactly(t, error2, writer.closed, 'the writable stream must be errored with error2')
        .then(() => {
          writer.releaseLock();
          assert_array_equals(ws.events, []);
          return ws;
        });
  });
}

promise_test(t => {
  const rs = recordingReadableStream({
    start(c) {
      c.error(error1);
    }
  });
  const ws = recordingWritableStream({
    start(c) {
      c.error(error2);
    }
  });

  // Trying to abort a stream that is erroring will give the writable's error
  return promise_rejects_exactly(t, error2, rs.pipeTo(ws), 'pipeTo must reject with the writable stream\'s error').then(() => {
    assert_array_equals(rs.events, []);
    assert_array_equals(ws.events, []);

    return Promise.all([
      promise_rejects_exactly(t, error1, rs.getReader().closed, 'the readable stream must be errored with error1'),
      promise_rejects_exactly(t, error2, ws.getWriter().closed, 'the writable stream must be errored with error2')
    ]);
  });

}, 'Piping from an errored readable stream to an erroring writable stream');

promise_test(t => {
  const rs = recordingReadableStream({
    start(c) {
      c.error(error1);
    }
  });

  return createErroredWritableStream(t)
      .then(ws => promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the readable stream\'s error'))
      .then(() => {
        assert_array_equals(rs.events, []);

        return promise_rejects_exactly(t, error1, rs.getReader().closed, 'the readable stream must be errored with error1');
      });
}, 'Piping from an errored readable stream to an errored writable stream');

promise_test(t => {
  const rs = recordingReadableStream({
    start(c) {
      c.error(error1);
    }
  });
  const ws = recordingWritableStream({
    start(c) {
      c.error(error2);
    }
  });

  return promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: true }),
    'pipeTo must reject with the readable stream\'s error')
  .then(() => {
    assert_array_equals(rs.events, []);
    assert_array_equals(ws.events, []);

    return Promise.all([
      promise_rejects_exactly(t, error1, rs.getReader().closed, 'the readable stream must be errored with error1'),
      promise_rejects_exactly(t, error2, ws.getWriter().closed, 'the writable stream must be errored with error2')
    ]);
  });

}, 'Piping from an errored readable stream to an erroring writable stream; preventAbort = true');

promise_test(t => {
  const rs = recordingReadableStream({
    start(c) {
      c.error(error1);
    }
  });
  return createErroredWritableStream(t)
  .then(ws => promise_rejects_exactly(t, error1, rs.pipeTo(ws, { preventAbort: true }),
                                      'pipeTo must reject with the readable stream\'s error'))
  .then(() => {
    assert_array_equals(rs.events, []);

    return promise_rejects_exactly(t, error1, rs.getReader().closed, 'the readable stream must be errored with error1');
  });

}, 'Piping from an errored readable stream to an errored writable stream; preventAbort = true');

promise_test(t => {
  const rs = recordingReadableStream({
    start(c) {
      c.error(error1);
    }
  });
  const ws = recordingWritableStream();
  const writer = ws.getWriter();
  const closePromise = writer.close();
  writer.releaseLock();

  return promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the readable stream\'s error').then(() => {
    assert_array_equals(rs.events, []);
    assert_array_equals(ws.events, ['abort', error1]);

    return Promise.all([
      promise_rejects_exactly(t, error1, rs.getReader().closed, 'the readable stream must be errored with error1'),
      promise_rejects_exactly(t, error1, ws.getWriter().closed,
        'closed must reject with error1'),
      promise_rejects_exactly(t, error1, closePromise,
        'close() must reject with error1')
    ]);
  });

}, 'Piping from an errored readable stream to a closing writable stream');

promise_test(t => {
  const rs = recordingReadableStream({
    start(c) {
      c.error(error1);
    }
  });
  const ws = recordingWritableStream();
  const writer = ws.getWriter();
  const closePromise = writer.close();
  writer.releaseLock();

  return flushAsyncEvents().then(() => {
    return promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the readable stream\'s error').then(() => {
      assert_array_equals(rs.events, []);
      assert_array_equals(ws.events, ['close']);

      return Promise.all([
        promise_rejects_exactly(t, error1, rs.getReader().closed, 'the readable stream must be errored with error1'),
        ws.getWriter().closed,
        closePromise
      ]);
    });
  });

}, 'Piping from an errored readable stream to a closed writable stream');

promise_test(t => {
  const rs = recordingReadableStream({
    start(c) {
      c.close();
    }
  });
  const ws = recordingWritableStream({
    start(c) {
      c.error(error1);
    }
  });

  return promise_rejects_exactly(t, error1, rs.pipeTo(ws), 'pipeTo must reject with the writable stream\'s error').then(() => {
    assert_array_equals(rs.events, []);
    assert_array_equals(ws.events, []);

    return Promise.all([
      rs.getReader().closed,
      promise_rejects_exactly(t, error1, ws.getWriter().closed, 'the writable stream must be errored with error1')
    ]);
  });

}, 'Piping from a closed readable stream to an erroring writable stream');

promise_test(t => {
  const rs = recordingReadableStream({
    start(c) {
      c.close();
    }
  });
  return createErroredWritableStream(t)
  .then(ws => promise_rejects_exactly(t, error2, rs.pipeTo(ws), 'pipeTo must reject with the writable stream\'s error'))
  .then(() => {
    assert_array_equals(rs.events, []);

    return rs.getReader().closed;
  });

}, 'Piping from a closed readable stream to an errored writable stream');

promise_test(() => {
  const rs = recordingReadableStream({
    start(c) {
      c.close();
    }
  });
  const ws = recordingWritableStream();
  const writer = ws.getWriter();
  writer.close();
  writer.releaseLock();

  return rs.pipeTo(ws).then(() => {
    assert_array_equals(rs.events, []);
    assert_array_equals(ws.events, ['close']);

    return Promise.all([
      rs.getReader().closed,
      ws.getWriter().closed
    ]);
  });

}, 'Piping from a closed readable stream to a closed writable stream');
