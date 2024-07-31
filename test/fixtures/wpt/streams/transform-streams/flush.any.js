// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
'use strict';

promise_test(() => {
  let flushCalled = false;
  const ts = new TransformStream({
    transform() { },
    flush() {
      flushCalled = true;
    }
  });

  return ts.writable.getWriter().close().then(() => {
    return assert_true(flushCalled, 'closing the writable triggers the transform flush immediately');
  });
}, 'TransformStream flush is called immediately when the writable is closed, if no writes are queued');

promise_test(() => {
  let flushCalled = false;
  let resolveTransform;
  const ts = new TransformStream({
    transform() {
      return new Promise(resolve => {
        resolveTransform = resolve;
      });
    },
    flush() {
      flushCalled = true;
      return new Promise(() => {}); // never resolves
    }
  }, undefined, { highWaterMark: 1 });

  const writer = ts.writable.getWriter();
  writer.write('a');
  writer.close();
  assert_false(flushCalled, 'closing the writable does not immediately call flush if writes are not finished');

  let rsClosed = false;
  ts.readable.getReader().closed.then(() => {
    rsClosed = true;
  });

  return delay(0).then(() => {
    assert_false(flushCalled, 'closing the writable does not asynchronously call flush if writes are not finished');
    resolveTransform();
    return delay(0);
  }).then(() => {
    assert_true(flushCalled, 'flush is eventually called');
    assert_false(rsClosed, 'if flushPromise does not resolve, the readable does not become closed');
  });
}, 'TransformStream flush is called after all queued writes finish, once the writable is closed');

promise_test(() => {
  let c;
  const ts = new TransformStream({
    start(controller) {
      c = controller;
    },
    transform() {
    },
    flush() {
      c.enqueue('x');
      c.enqueue('y');
    }
  });

  const reader = ts.readable.getReader();

  const writer = ts.writable.getWriter();
  writer.write('a');
  writer.close();
  return reader.read().then(result1 => {
    assert_equals(result1.value, 'x', 'the first chunk read is the first one enqueued in flush');
    assert_equals(result1.done, false, 'the first chunk read is the first one enqueued in flush');

    return reader.read().then(result2 => {
      assert_equals(result2.value, 'y', 'the second chunk read is the second one enqueued in flush');
      assert_equals(result2.done, false, 'the second chunk read is the second one enqueued in flush');
    });
  });
}, 'TransformStream flush gets a chance to enqueue more into the readable');

promise_test(() => {
  let c;
  const ts = new TransformStream({
    start(controller) {
      c = controller;
    },
    transform() {
    },
    flush() {
      c.enqueue('x');
      c.enqueue('y');
      return delay(0);
    }
  });

  const reader = ts.readable.getReader();

  const writer = ts.writable.getWriter();
  writer.write('a');
  writer.close();

  return Promise.all([
    reader.read().then(result1 => {
      assert_equals(result1.value, 'x', 'the first chunk read is the first one enqueued in flush');
      assert_equals(result1.done, false, 'the first chunk read is the first one enqueued in flush');

      return reader.read().then(result2 => {
        assert_equals(result2.value, 'y', 'the second chunk read is the second one enqueued in flush');
        assert_equals(result2.done, false, 'the second chunk read is the second one enqueued in flush');
      });
    }),
    reader.closed.then(() => {
      assert_true(true, 'readable reader becomes closed');
    })
  ]);
}, 'TransformStream flush gets a chance to enqueue more into the readable, and can then async close');

const error1 = new Error('error1');
error1.name = 'error1';

promise_test(t => {
  const ts = new TransformStream({
    flush(controller) {
      controller.error(error1);
    }
  });
  return promise_rejects_exactly(t, error1, ts.writable.getWriter().close(), 'close() should reject');
}, 'error() during flush should cause writer.close() to reject');

promise_test(async t => {
  let flushed = false;
  const ts = new TransformStream({
    flush() {
      flushed = true;
    },
    cancel: t.unreached_func('cancel should not be called')
  });
  const closePromise = ts.writable.close();
  await delay(0);
  const cancelPromise = ts.readable.cancel(error1);
  await Promise.all([closePromise, cancelPromise]);
  assert_equals(flushed, true, 'transformer.flush() should be called');
}, 'closing the writable side should call transformer.flush() and a parallel readable.cancel() should not reject');
