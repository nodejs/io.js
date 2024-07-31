// META: global=window,worker,shadowrealm
// META: script=../resources/recording-streams.js
// META: script=../resources/test-utils.js
'use strict';

promise_test(t => {
  const ts = recordingTransformStream({}, undefined, { highWaterMark: 0 });
  const rs = new ReadableStream({
    start(controller) {
      controller.enqueue(0);
    }
  });
  let pipeToRejected = false;
  const pipeToPromise = promise_rejects_js(t, TypeError, rs.pipeTo(ts.writable), 'pipeTo should reject').then(() => {
    pipeToRejected = true;
  });
  return delay(0).then(() => {
    assert_array_equals(ts.events, [], 'transform() should have seen no chunks');
    assert_false(pipeToRejected, 'pipeTo() should not have rejected yet');
    ts.controller.terminate();
    return pipeToPromise;
  }).then(() => {
    assert_array_equals(ts.events, [], 'transform() should still have seen no chunks');
    assert_true(pipeToRejected, 'pipeToRejected must be true');
  });
}, 'controller.terminate() should error pipeTo()');

promise_test(t => {
  const ts = recordingTransformStream({}, undefined, { highWaterMark: 1 });
  const rs = new ReadableStream({
    start(controller) {
      controller.enqueue(0);
      controller.enqueue(1);
    }
  });
  const pipeToPromise = rs.pipeTo(ts.writable);
  return delay(0).then(() => {
    assert_array_equals(ts.events, ['transform', 0], 'transform() should have seen one chunk');
    ts.controller.terminate();
    return promise_rejects_js(t, TypeError, pipeToPromise, 'pipeTo() should reject');
  }).then(() => {
    assert_array_equals(ts.events, ['transform', 0], 'transform() should still have seen only one chunk');
  });
}, 'controller.terminate() should prevent remaining chunks from being processed');

test(() => {
  new TransformStream({
    start(controller) {
      controller.enqueue(0);
      controller.terminate();
      assert_throws_js(TypeError, () => controller.enqueue(1), 'enqueue should throw');
    }
  });
}, 'controller.enqueue() should throw after controller.terminate()');

const error1 = new Error('error1');
error1.name = 'error1';

promise_test(t => {
  const ts = new TransformStream({
    start(controller) {
      controller.enqueue(0);
      controller.terminate();
      controller.error(error1);
    }
  });
  return Promise.all([
    promise_rejects_js(t, TypeError, ts.writable.abort(), 'abort() should reject with a TypeError'),
    promise_rejects_exactly(t, error1, ts.readable.cancel(), 'cancel() should reject with error1'),
    promise_rejects_exactly(t, error1, ts.readable.getReader().closed, 'closed should reject with error1')
  ]);
}, 'controller.error() after controller.terminate() with queued chunk should error the readable');

promise_test(t => {
  const ts = new TransformStream({
    start(controller) {
      controller.terminate();
      controller.error(error1);
    }
  });
  return Promise.all([
    promise_rejects_js(t, TypeError, ts.writable.abort(), 'abort() should reject with a TypeError'),
    ts.readable.cancel(),
    ts.readable.getReader().closed
  ]);
}, 'controller.error() after controller.terminate() without queued chunk should do nothing');

promise_test(() => {
  const ts = new TransformStream({
    flush(controller) {
      controller.terminate();
    }
  });
  const writer = ts.writable.getWriter();
  return Promise.all([
    writer.close(),
    writer.closed,
    ts.readable.getReader().closed
  ]);
}, 'controller.terminate() inside flush() should not prevent writer.close() from succeeding');
