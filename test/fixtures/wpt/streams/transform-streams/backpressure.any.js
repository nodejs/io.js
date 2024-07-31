// META: global=window,worker,shadowrealm
// META: script=../resources/recording-streams.js
// META: script=../resources/test-utils.js
'use strict';

const error1 = new Error('error1 message');
error1.name = 'error1';

promise_test(() => {
  const ts = recordingTransformStream();
  const writer = ts.writable.getWriter();
  // This call never resolves.
  writer.write('a');
  return flushAsyncEvents().then(() => {
    assert_array_equals(ts.events, [], 'transform should not be called');
  });
}, 'backpressure allows no transforms with a default identity transform and no reader');

promise_test(() => {
  const ts = recordingTransformStream({}, undefined, { highWaterMark: 1 });
  const writer = ts.writable.getWriter();
  // This call to write() resolves asynchronously.
  writer.write('a');
  // This call to write() waits for backpressure that is never relieved and never calls transform().
  writer.write('b');
  return flushAsyncEvents().then(() => {
    assert_array_equals(ts.events, ['transform', 'a'], 'transform should be called once');
  });
}, 'backpressure only allows one transform() with a identity transform with a readable HWM of 1 and no reader');

promise_test(() => {
  // Without a transform() implementation, recordingTransformStream() never enqueues anything.
  const ts = recordingTransformStream({
    transform() {
      // Discard all chunks. As a result, the readable side is never full enough to exert backpressure and transform()
      // keeps being called.
    }
  }, undefined, { highWaterMark: 1 });
  const writer = ts.writable.getWriter();
  const writePromises = [];
  for (let i = 0; i < 4; ++i) {
    writePromises.push(writer.write(i));
  }
  return Promise.all(writePromises).then(() => {
    assert_array_equals(ts.events, ['transform', 0, 'transform', 1, 'transform', 2, 'transform', 3],
                        'all 4 events should be transformed');
  });
}, 'transform() should keep being called as long as there is no backpressure');

promise_test(() => {
  const ts = new TransformStream({}, undefined, { highWaterMark: 1 });
  const writer = ts.writable.getWriter();
  const reader = ts.readable.getReader();
  const events = [];
  const writerPromises = [
    writer.write('a').then(() => events.push('a')),
    writer.write('b').then(() => events.push('b')),
    writer.close().then(() => events.push('closed'))];
  return delay(0).then(() => {
    assert_array_equals(events, ['a'], 'the first write should have resolved');
    return reader.read();
  }).then(({ value, done }) => {
    assert_false(done, 'done should not be true');
    assert_equals('a', value, 'value should be "a"');
    return delay(0);
  }).then(() => {
    assert_array_equals(events, ['a', 'b', 'closed'], 'both writes and close() should have resolved');
    return reader.read();
  }).then(({ value, done }) => {
    assert_false(done, 'done should still not be true');
    assert_equals('b', value, 'value should be "b"');
    return reader.read();
  }).then(({ done }) => {
    assert_true(done, 'done should be true');
    return writerPromises;
  });
}, 'writes should resolve as soon as transform completes');

promise_test(() => {
  const ts = new TransformStream(undefined, undefined, { highWaterMark: 0 });
  const writer = ts.writable.getWriter();
  const reader = ts.readable.getReader();
  const readPromise = reader.read();
  writer.write('a');
  return readPromise.then(({ value, done }) => {
    assert_false(done, 'not done');
    assert_equals(value, 'a', 'value should be "a"');
  });
}, 'calling pull() before the first write() with backpressure should work');

promise_test(() => {
  let reader;
  const ts = recordingTransformStream({
    transform(chunk, controller) {
      controller.enqueue(chunk);
      return reader.read();
    }
  }, undefined, { highWaterMark: 1 });
  const writer = ts.writable.getWriter();
  reader = ts.readable.getReader();
  return writer.write('a');
}, 'transform() should be able to read the chunk it just enqueued');

promise_test(() => {
  let resolveTransform;
  const transformPromise = new Promise(resolve => {
    resolveTransform = resolve;
  });
  const ts = recordingTransformStream({
    transform() {
      return transformPromise;
    }
  }, undefined, new CountQueuingStrategy({ highWaterMark: Infinity }));
  const writer = ts.writable.getWriter();
  assert_equals(writer.desiredSize, 1, 'desiredSize should be 1');
  return delay(0).then(() => {
    writer.write('a');
    assert_array_equals(ts.events, ['transform', 'a']);
    assert_equals(writer.desiredSize, 0, 'desiredSize should be 0');
    return flushAsyncEvents();
  }).then(() => {
    assert_equals(writer.desiredSize, 0, 'desiredSize should still be 0');
    resolveTransform();
    return delay(0);
  }).then(() => {
    assert_equals(writer.desiredSize, 1, 'desiredSize should be 1');
  });
}, 'blocking transform() should cause backpressure');

promise_test(t => {
  const ts = new TransformStream();
  ts.readable.cancel(error1);
  return promise_rejects_exactly(t, error1, ts.writable.getWriter().closed, 'closed should reject');
}, 'writer.closed should resolve after readable is canceled during start');

promise_test(t => {
  const ts = new TransformStream({}, undefined, { highWaterMark: 0 });
  return delay(0).then(() => {
    ts.readable.cancel(error1);
    return promise_rejects_exactly(t, error1, ts.writable.getWriter().closed, 'closed should reject');
  });
}, 'writer.closed should resolve after readable is canceled with backpressure');

promise_test(t => {
  const ts = new TransformStream({}, undefined, { highWaterMark: 1 });
  return delay(0).then(() => {
    ts.readable.cancel(error1);
    return promise_rejects_exactly(t, error1, ts.writable.getWriter().closed, 'closed should reject');
  });
}, 'writer.closed should resolve after readable is canceled with no backpressure');

promise_test(() => {
  const ts = new TransformStream({}, undefined, { highWaterMark: 1 });
  const writer = ts.writable.getWriter();
  return delay(0).then(() => {
    const writePromise = writer.write('a');
    ts.readable.cancel(error1);
    return writePromise;
  });
}, 'cancelling the readable should cause a pending write to resolve');

promise_test(t => {
  const rs = new ReadableStream();
  const ts = new TransformStream();
  const pipePromise = rs.pipeTo(ts.writable);
  ts.readable.cancel(error1);
  return promise_rejects_exactly(t, error1, pipePromise, 'promise returned from pipeTo() should be rejected');
}, 'cancelling the readable side of a TransformStream should abort an empty pipe');

promise_test(t => {
  const rs = new ReadableStream();
  const ts = new TransformStream();
  const pipePromise = rs.pipeTo(ts.writable);
  return delay(0).then(() => {
    ts.readable.cancel(error1);
    return promise_rejects_exactly(t, error1, pipePromise, 'promise returned from pipeTo() should be rejected');
  });
}, 'cancelling the readable side of a TransformStream should abort an empty pipe after startup');

promise_test(t => {
  const rs = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.enqueue('b');
      controller.enqueue('c');
    }
  });
  const ts = new TransformStream();
  const pipePromise = rs.pipeTo(ts.writable);
  // Allow data to flow into the pipe.
  return delay(0).then(() => {
    ts.readable.cancel(error1);
    return promise_rejects_exactly(t, error1, pipePromise, 'promise returned from pipeTo() should be rejected');
  });
}, 'cancelling the readable side of a TransformStream should abort a full pipe');
