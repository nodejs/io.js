// META: global=window,worker,shadowrealm
'use strict';

// Due to the limitations of floating-point precision, the calculation of desiredSize sometimes gives different answers
// than adding up the items in the queue would. It is important that implementations give the same result in these edge
// cases so that developers do not come to depend on non-standard behaviour. See
// https://github.com/whatwg/streams/issues/582 and linked issues for further discussion.

promise_test(() => {
  const writer = setupTestStream();

  const writePromises = [
    writer.write(2),
    writer.write(Number.MAX_SAFE_INTEGER)
  ];

  assert_equals(writer.desiredSize, 0 - 2 - Number.MAX_SAFE_INTEGER,
    'desiredSize must be calculated using double-precision floating-point arithmetic (after writing two chunks)');

  return Promise.all(writePromises).then(() => {
    assert_equals(writer.desiredSize, 0, '[[queueTotalSize]] must clamp to 0 if it becomes negative');
  });
}, 'Floating point arithmetic must manifest near NUMBER.MAX_SAFE_INTEGER (total ends up positive)');

promise_test(() => {
  const writer = setupTestStream();

  const writePromises = [
    writer.write(1e-16),
    writer.write(1)
  ];

  assert_equals(writer.desiredSize, 0 - 1e-16 - 1,
    'desiredSize must be calculated using double-precision floating-point arithmetic (after writing two chunks)');

  return Promise.all(writePromises).then(() => {
    assert_equals(writer.desiredSize, 0, '[[queueTotalSize]] must clamp to 0 if it becomes negative');
  });
}, 'Floating point arithmetic must manifest near 0 (total ends up positive, but clamped)');

promise_test(() => {
  const writer = setupTestStream();

  const writePromises = [
    writer.write(1e-16),
    writer.write(1),
    writer.write(2e-16)
  ];

  assert_equals(writer.desiredSize, 0 - 1e-16 - 1 - 2e-16,
    'desiredSize must be calculated using double-precision floating-point arithmetic (after writing three chunks)');

  return Promise.all(writePromises).then(() => {
    assert_equals(writer.desiredSize, 0 - 1e-16 - 1 - 2e-16 + 1e-16 + 1 + 2e-16,
      'desiredSize must be calculated using floating-point arithmetic (after the three chunks have finished writing)');
  });
}, 'Floating point arithmetic must manifest near 0 (total ends up positive, and not clamped)');

promise_test(() => {
  const writer = setupTestStream();

  const writePromises = [
    writer.write(2e-16),
    writer.write(1)
  ];

  assert_equals(writer.desiredSize, 0 - 2e-16 - 1,
    'desiredSize must be calculated using double-precision floating-point arithmetic (after writing two chunks)');

  return Promise.all(writePromises).then(() => {
    assert_equals(writer.desiredSize, 0 - 2e-16 - 1 + 2e-16 + 1,
      'desiredSize must be calculated using floating-point arithmetic (after the two chunks have finished writing)');
  });
}, 'Floating point arithmetic must manifest near 0 (total ends up zero)');

function setupTestStream() {
  const strategy = {
    size(x) {
      return x;
    },
    highWaterMark: 0
  };

  const ws = new WritableStream({}, strategy);

  return ws.getWriter();
}
