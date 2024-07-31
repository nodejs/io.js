// META: global=window,worker,shadowrealm
// META: script=../resources/recording-streams.js
// META: script=../resources/test-utils.js
'use strict';

// Here we just test that the strategies are correctly passed to the readable and writable sides. We assume that
// ReadableStream and WritableStream will correctly apply the strategies when they are being used by a TransformStream
// and so it isn't necessary to repeat their tests here.

test(() => {
  const ts = new TransformStream({}, { highWaterMark: 17 });
  assert_equals(ts.writable.getWriter().desiredSize, 17, 'desiredSize should be 17');
}, 'writableStrategy highWaterMark should work');

promise_test(() => {
  const ts = recordingTransformStream({}, undefined, { highWaterMark: 9 });
  const writer = ts.writable.getWriter();
  for (let i = 0; i < 10; ++i) {
    writer.write(i);
  }
  return delay(0).then(() => {
    assert_array_equals(ts.events, [
      'transform', 0, 'transform', 1, 'transform', 2, 'transform', 3, 'transform', 4,
      'transform', 5, 'transform', 6, 'transform', 7, 'transform', 8],
                        'transform() should have been called 9 times');
  });
}, 'readableStrategy highWaterMark should work');

promise_test(t => {
  let writableSizeCalled = false;
  let readableSizeCalled = false;
  let transformCalled = false;
  const ts = new TransformStream(
    {
      transform(chunk, controller) {
        t.step(() => {
          transformCalled = true;
          assert_true(writableSizeCalled, 'writableStrategy.size() should have been called');
          assert_false(readableSizeCalled, 'readableStrategy.size() should not have been called');
          controller.enqueue(chunk);
          assert_true(readableSizeCalled, 'readableStrategy.size() should have been called');
        });
      }
    },
    {
      size() {
        writableSizeCalled = true;
        return 1;
      }
    },
    {
      size() {
        readableSizeCalled = true;
        return 1;
      },
      highWaterMark: Infinity
    });
  return ts.writable.getWriter().write().then(() => {
    assert_true(transformCalled, 'transform() should be called');
  });
}, 'writable should have the correct size() function');

test(() => {
  const ts = new TransformStream();
  const writer = ts.writable.getWriter();
  assert_equals(writer.desiredSize, 1, 'default writable HWM is 1');
  writer.write(undefined);
  assert_equals(writer.desiredSize, 0, 'default chunk size is 1');
}, 'default writable strategy should be equivalent to { highWaterMark: 1 }');

promise_test(t => {
  const ts = new TransformStream({
    transform(chunk, controller) {
      return t.step(() => {
        assert_equals(controller.desiredSize, 0, 'desiredSize should be 0');
        controller.enqueue(undefined);
        // The first chunk enqueued is consumed by the pending read().
        assert_equals(controller.desiredSize, 0, 'desiredSize should still be 0');
        controller.enqueue(undefined);
        assert_equals(controller.desiredSize, -1, 'desiredSize should be -1');
      });
    }
  });
  const writePromise = ts.writable.getWriter().write();
  return ts.readable.getReader().read().then(() => writePromise);
}, 'default readable strategy should be equivalent to { highWaterMark: 0 }');

test(() => {
  assert_throws_js(RangeError, () => new TransformStream(undefined, { highWaterMark: -1 }),
                   'should throw RangeError for negative writableHighWaterMark');
  assert_throws_js(RangeError, () => new TransformStream(undefined, undefined, { highWaterMark: -1 }),
                   'should throw RangeError for negative readableHighWaterMark');
  assert_throws_js(RangeError, () => new TransformStream(undefined, { highWaterMark: NaN }),
                   'should throw RangeError for NaN writableHighWaterMark');
  assert_throws_js(RangeError, () => new TransformStream(undefined, undefined, { highWaterMark: NaN }),
                   'should throw RangeError for NaN readableHighWaterMark');
}, 'a RangeError should be thrown for an invalid highWaterMark');

const objectThatConvertsTo42 = {
  toString() {
    return '42';
  }
};

test(() => {
  const ts = new TransformStream(undefined, { highWaterMark: objectThatConvertsTo42 });
  const writer = ts.writable.getWriter();
  assert_equals(writer.desiredSize, 42, 'writable HWM is 42');
}, 'writableStrategy highWaterMark should be converted to a number');

test(() => {
  const ts = new TransformStream({
    start(controller) {
      assert_equals(controller.desiredSize, 42, 'desiredSize should be 42');
    }
  }, undefined, { highWaterMark: objectThatConvertsTo42 });
}, 'readableStrategy highWaterMark should be converted to a number');

promise_test(t => {
  const ts = new TransformStream(undefined, undefined, {
    size() { return NaN; },
    highWaterMark: 1
  });
  const writer = ts.writable.getWriter();
  return promise_rejects_js(t, RangeError, writer.write(), 'write should reject');
}, 'a bad readableStrategy size function should cause writer.write() to reject on an identity transform');

promise_test(t => {
  const ts = new TransformStream({
    transform(chunk, controller) {
      // This assert has the important side-effect of catching the error, so transform() does not throw.
      assert_throws_js(RangeError, () => controller.enqueue(chunk), 'enqueue should throw');
    }
  }, undefined, {
    size() {
      return -1;
    },
    highWaterMark: 1
  });

  const writer = ts.writable.getWriter();
  return writer.write().then(() => {
    return Promise.all([
      promise_rejects_js(t, RangeError, writer.ready, 'ready should reject'),
      promise_rejects_js(t, RangeError, writer.closed, 'closed should reject'),
      promise_rejects_js(t, RangeError, ts.readable.getReader().closed, 'readable closed should reject')
    ]);
  });
}, 'a bad readableStrategy size function should error the stream on enqueue even when transformer.transform() ' +
   'catches the exception');
