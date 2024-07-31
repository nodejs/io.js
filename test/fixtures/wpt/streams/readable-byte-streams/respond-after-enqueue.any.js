// META: global=window,worker,shadowrealm

'use strict';

// Repro for Blink bug https://crbug.com/1255762.
promise_test(async () => {
  const rs = new ReadableStream({
    type: 'bytes',
    autoAllocateChunkSize: 10,
    pull(controller) {
      controller.enqueue(new Uint8Array([1, 2, 3]));
      controller.byobRequest.respond(10);
    }
  });

  const reader = rs.getReader();
  const {value, done} = await reader.read();
  assert_false(done, 'done should not be true');
  assert_array_equals(value, [1, 2, 3], 'value should be 3 bytes');
}, 'byobRequest.respond() after enqueue() should not crash');

promise_test(async () => {
  const rs = new ReadableStream({
    type: 'bytes',
    autoAllocateChunkSize: 10,
    pull(controller) {
      const byobRequest = controller.byobRequest;
      controller.enqueue(new Uint8Array([1, 2, 3]));
      byobRequest.respond(10);
    }
  });

  const reader = rs.getReader();
  const {value, done} = await reader.read();
  assert_false(done, 'done should not be true');
  assert_array_equals(value, [1, 2, 3], 'value should be 3 bytes');
}, 'byobRequest.respond() with cached byobRequest after enqueue() should not crash');

promise_test(async () => {
  const rs = new ReadableStream({
    type: 'bytes',
    autoAllocateChunkSize: 10,
    pull(controller) {
      controller.enqueue(new Uint8Array([1, 2, 3]));
      controller.byobRequest.respond(2);
    }
  });

  const reader = rs.getReader();
  const [read1, read2] = await Promise.all([reader.read(), reader.read()]);
  assert_false(read1.done, 'read1.done should not be true');
  assert_array_equals(read1.value, [1, 2, 3], 'read1.value should be 3 bytes');
  assert_false(read2.done, 'read2.done should not be true');
  assert_array_equals(read2.value, [0, 0], 'read2.value should be 2 bytes');
}, 'byobRequest.respond() after enqueue() with double read should not crash');
