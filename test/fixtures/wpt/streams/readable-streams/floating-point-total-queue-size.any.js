// META: global=window,worker,shadowrealm
'use strict';

// Due to the limitations of floating-point precision, the calculation of desiredSize sometimes gives different answers
// than adding up the items in the queue would. It is important that implementations give the same result in these edge
// cases so that developers do not come to depend on non-standard behaviour. See
// https://github.com/whatwg/streams/issues/582 and linked issues for further discussion.

promise_test(() => {
  const { reader, controller } = setupTestStream();

  controller.enqueue(2);
  assert_equals(controller.desiredSize, 0 - 2, 'desiredSize must be -2 after enqueueing such a chunk');

  controller.enqueue(Number.MAX_SAFE_INTEGER);
  assert_equals(controller.desiredSize, 0 - Number.MAX_SAFE_INTEGER - 2,
    'desiredSize must be calculated using double-precision floating-point arithmetic (adding a second chunk)');

  return reader.read().then(() => {
    assert_equals(controller.desiredSize, 0 - Number.MAX_SAFE_INTEGER - 2 + 2,
      'desiredSize must be calculated using double-precision floating-point arithmetic (subtracting a chunk)');

    return reader.read();
  }).then(() => {
    assert_equals(controller.desiredSize, 0, '[[queueTotalSize]] must clamp to 0 if it becomes negative');
  });
}, 'Floating point arithmetic must manifest near NUMBER.MAX_SAFE_INTEGER (total ends up positive)');

promise_test(() => {
  const { reader, controller } = setupTestStream();

  controller.enqueue(1e-16);
  assert_equals(controller.desiredSize, 0 - 1e-16, 'desiredSize must be -1e16 after enqueueing such a chunk');

  controller.enqueue(1);
  assert_equals(controller.desiredSize, 0 - 1e-16 - 1,
    'desiredSize must be calculated using double-precision floating-point arithmetic (adding a second chunk)');

  return reader.read().then(() => {
    assert_equals(controller.desiredSize, 0 - 1e-16 - 1 + 1e-16,
      'desiredSize must be calculated using double-precision floating-point arithmetic (subtracting a chunk)');

    return reader.read();
  }).then(() => {
    assert_equals(controller.desiredSize, 0, '[[queueTotalSize]] must clamp to 0 if it becomes negative');
  });
}, 'Floating point arithmetic must manifest near 0 (total ends up positive, but clamped)');

promise_test(() => {
  const { reader, controller } = setupTestStream();

  controller.enqueue(1e-16);
  assert_equals(controller.desiredSize, 0 - 1e-16, 'desiredSize must be -2e16 after enqueueing such a chunk');

  controller.enqueue(1);
  assert_equals(controller.desiredSize, 0 - 1e-16 - 1,
    'desiredSize must be calculated using double-precision floating-point arithmetic (adding a second chunk)');

  controller.enqueue(2e-16);
  assert_equals(controller.desiredSize, 0 - 1e-16 - 1 - 2e-16,
    'desiredSize must be calculated using double-precision floating-point arithmetic (adding a third chunk)');

  return reader.read().then(() => {
    assert_equals(controller.desiredSize, 0 - 1e-16 - 1 - 2e-16 + 1e-16,
      'desiredSize must be calculated using double-precision floating-point arithmetic (subtracting a chunk)');

    return reader.read();
  }).then(() => {
    assert_equals(controller.desiredSize, 0 - 1e-16 - 1 - 2e-16 + 1e-16 + 1,
      'desiredSize must be calculated using double-precision floating-point arithmetic (subtracting a second chunk)');

    return reader.read();
  }).then(() => {
    assert_equals(controller.desiredSize, 0 - 1e-16 - 1 - 2e-16 + 1e-16 + 1 + 2e-16,
      'desiredSize must be calculated using double-precision floating-point arithmetic (subtracting a third chunk)');
  });
}, 'Floating point arithmetic must manifest near 0 (total ends up positive, and not clamped)');

promise_test(() => {
  const { reader, controller } = setupTestStream();

  controller.enqueue(2e-16);
  assert_equals(controller.desiredSize, 0 - 2e-16, 'desiredSize must be -2e16 after enqueueing such a chunk');

  controller.enqueue(1);
  assert_equals(controller.desiredSize, 0 - 2e-16 - 1,
    'desiredSize must be calculated using double-precision floating-point arithmetic (adding a second chunk)');

  return reader.read().then(() => {
    assert_equals(controller.desiredSize, 0 - 2e-16 - 1 + 2e-16,
      'desiredSize must be calculated using double-precision floating-point arithmetic (subtracting a chunk)');

    return reader.read();
  }).then(() => {
    assert_equals(controller.desiredSize, 0,
      'desiredSize must be calculated using double-precision floating-point arithmetic (subtracting a second chunk)');
  });
}, 'Floating point arithmetic must manifest near 0 (total ends up zero)');

function setupTestStream() {
  const strategy = {
    size(x) {
      return x;
    },
    highWaterMark: 0
  };

  let controller;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  }, strategy);

  return { reader: rs.getReader(), controller };
}
