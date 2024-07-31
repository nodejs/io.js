// META: global=window,worker,shadowrealm
// META: script=../resources/recording-streams.js
'use strict';

test(() => {

  const rs = new ReadableStream();
  const ws = new WritableStream();

  assert_false(rs.locked, 'sanity check: the ReadableStream must not start locked');
  assert_false(ws.locked, 'sanity check: the WritableStream must not start locked');

  rs.pipeTo(ws);

  assert_true(rs.locked, 'the ReadableStream must become locked');
  assert_true(ws.locked, 'the WritableStream must become locked');

}, 'Piping must lock both the ReadableStream and WritableStream');

promise_test(() => {

  const rs = new ReadableStream({
    start(controller) {
      controller.close();
    }
  });
  const ws = new WritableStream();

  return rs.pipeTo(ws).then(() => {
    assert_false(rs.locked, 'the ReadableStream must become unlocked');
    assert_false(ws.locked, 'the WritableStream must become unlocked');
  });

}, 'Piping finishing must unlock both the ReadableStream and WritableStream');

promise_test(t => {

  const fakeRS = Object.create(ReadableStream.prototype);
  const ws = new WritableStream();

  return promise_rejects_js(t, TypeError, ReadableStream.prototype.pipeTo.apply(fakeRS, [ws]),
    'pipeTo should reject with a TypeError');

}, 'pipeTo must check the brand of its ReadableStream this value');

promise_test(t => {

  const rs = new ReadableStream();
  const fakeWS = Object.create(WritableStream.prototype);

  return promise_rejects_js(t, TypeError, ReadableStream.prototype.pipeTo.apply(rs, [fakeWS]),
    'pipeTo should reject with a TypeError');

}, 'pipeTo must check the brand of its WritableStream argument');

promise_test(t => {

  const rs = new ReadableStream();
  const ws = new WritableStream();

  rs.getReader();

  assert_true(rs.locked, 'sanity check: the ReadableStream starts locked');
  assert_false(ws.locked, 'sanity check: the WritableStream does not start locked');

  return promise_rejects_js(t, TypeError, rs.pipeTo(ws)).then(() => {
    assert_false(ws.locked, 'the WritableStream must still be unlocked');
  });

}, 'pipeTo must fail if the ReadableStream is locked, and not lock the WritableStream');

promise_test(t => {

  const rs = new ReadableStream();
  const ws = new WritableStream();

  ws.getWriter();

  assert_false(rs.locked, 'sanity check: the ReadableStream does not start locked');
  assert_true(ws.locked, 'sanity check: the WritableStream starts locked');

  return promise_rejects_js(t, TypeError, rs.pipeTo(ws)).then(() => {
    assert_false(rs.locked, 'the ReadableStream must still be unlocked');
  });

}, 'pipeTo must fail if the WritableStream is locked, and not lock the ReadableStream');

promise_test(() => {

  const CHUNKS = 10;

  const rs = new ReadableStream({
    start(c) {
      for (let i = 0; i < CHUNKS; ++i) {
        c.enqueue(i);
      }
      c.close();
    }
  });

  const written = [];
  const ws = new WritableStream({
    write(chunk) {
      written.push(chunk);
    },
    close() {
      written.push('closed');
    }
  }, new CountQueuingStrategy({ highWaterMark: CHUNKS }));

  return rs.pipeTo(ws).then(() => {
    const targetValues = [];
    for (let i = 0; i < CHUNKS; ++i) {
      targetValues.push(i);
    }
    targetValues.push('closed');

    assert_array_equals(written, targetValues, 'the correct values must be written');

    // Ensure both readable and writable are closed by the time the pipe finishes.
    return Promise.all([
      rs.getReader().closed,
      ws.getWriter().closed
    ]);
  });

  // NOTE: no requirement on *when* the pipe finishes; that is left to implementations.

}, 'Piping from a ReadableStream from which lots of chunks are synchronously readable');

promise_test(t => {

  let controller;
  const rs = recordingReadableStream({
    start(c) {
      controller = c;
    }
  });

  const ws = recordingWritableStream();

  const pipePromise = rs.pipeTo(ws).then(() => {
    assert_array_equals(ws.events, ['write', 'Hello', 'close']);
  });

  t.step_timeout(() => {
    controller.enqueue('Hello');
    t.step_timeout(() => controller.close(), 10);
  }, 10);

  return pipePromise;

}, 'Piping from a ReadableStream for which a chunk becomes asynchronously readable after the pipeTo');

for (const preventAbort of [true, false]) {
  promise_test(() => {

    const rs = new ReadableStream({
      pull() {
        return Promise.reject(undefined);
      }
    });

    return rs.pipeTo(new WritableStream(), { preventAbort }).then(
        () => assert_unreached('pipeTo promise should be rejected'),
        value => assert_equals(value, undefined, 'rejection value should be undefined'));

  }, `an undefined rejection from pull should cause pipeTo() to reject when preventAbort is ${preventAbort}`);
}

for (const preventCancel of [true, false]) {
  promise_test(() => {

    const rs = new ReadableStream({
      pull(controller) {
        controller.enqueue(0);
      }
    });

    const ws = new WritableStream({
      write() {
        return Promise.reject(undefined);
      }
    });

    return rs.pipeTo(ws, { preventCancel }).then(
         () => assert_unreached('pipeTo promise should be rejected'),
        value => assert_equals(value, undefined, 'rejection value should be undefined'));

  }, `an undefined rejection from write should cause pipeTo() to reject when preventCancel is ${preventCancel}`);
}

promise_test(t => {
  const rs = new ReadableStream();
  const ws = new WritableStream();
  return promise_rejects_js(t, TypeError, rs.pipeTo(ws, {
    get preventAbort() {
      ws.getWriter();
    }
  }), 'pipeTo should reject');
}, 'pipeTo() should reject if an option getter grabs a writer');

promise_test(t => {
  const rs = new ReadableStream({
    start(controller) {
      controller.close();
    }
  });
  const ws = new WritableStream();

  return rs.pipeTo(ws, null);
}, 'pipeTo() promise should resolve if null is passed');
