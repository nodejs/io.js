// META: global=window,worker,shadowrealm
// META: script=../resources/rs-utils.js
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
// META: script=../resources/rs-test-templates.js
'use strict';

test(() => {

  const rs = new ReadableStream({ type: 'bytes' });
  const result = rs.tee();

  assert_true(Array.isArray(result), 'return value should be an array');
  assert_equals(result.length, 2, 'array should have length 2');
  assert_equals(result[0].constructor, ReadableStream, '0th element should be a ReadableStream');
  assert_equals(result[1].constructor, ReadableStream, '1st element should be a ReadableStream');

}, 'ReadableStream teeing with byte source: rs.tee() returns an array of two ReadableStreams');

promise_test(async t => {

  const rs = new ReadableStream({
    type: 'bytes',
    start(c) {
      c.enqueue(new Uint8Array([0x01]));
      c.enqueue(new Uint8Array([0x02]));
      c.close();
    }
  });

  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader({ mode: 'byob' });
  const reader2 = branch2.getReader({ mode: 'byob' });

  reader2.closed.then(t.unreached_func('branch2 should not be closed'));

  {
    const result = await reader1.read(new Uint8Array(1));
    assert_equals(result.done, false, 'done');
    assert_typed_array_equals(result.value, new Uint8Array([0x01]), 'value');
  }

  {
    const result = await reader1.read(new Uint8Array(1));
    assert_equals(result.done, false, 'done');
    assert_typed_array_equals(result.value, new Uint8Array([0x02]), 'value');
  }

  {
    const result = await reader1.read(new Uint8Array(1));
    assert_equals(result.done, true, 'done');
    assert_typed_array_equals(result.value, new Uint8Array([0]).subarray(0, 0), 'value');
  }

  {
    const result = await reader2.read(new Uint8Array(1));
    assert_equals(result.done, false, 'done');
    assert_typed_array_equals(result.value, new Uint8Array([0x01]), 'value');
  }

  await reader1.closed;

}, 'ReadableStream teeing with byte source: should be able to read one branch to the end without affecting the other');

promise_test(async () => {

  let pullCount = 0;
  const enqueuedChunk = new Uint8Array([0x01]);
  const rs = new ReadableStream({
    type: 'bytes',
    pull(c) {
      ++pullCount;
      if (pullCount === 1) {
        c.enqueue(enqueuedChunk);
      }
    }
  });

  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader();
  const reader2 = branch2.getReader();

  const [result1, result2] = await Promise.all([reader1.read(), reader2.read()]);
  assert_equals(result1.done, false, 'reader1 done');
  assert_equals(result2.done, false, 'reader2 done');

  const view1 = result1.value;
  const view2 = result2.value;
  assert_typed_array_equals(view1, new Uint8Array([0x01]), 'reader1 value');
  assert_typed_array_equals(view2, new Uint8Array([0x01]), 'reader2 value');

  assert_not_equals(view1.buffer, view2.buffer, 'chunks should have different buffers');
  assert_not_equals(enqueuedChunk.buffer, view1.buffer, 'enqueued chunk and branch1\'s chunk should have different buffers');
  assert_not_equals(enqueuedChunk.buffer, view2.buffer, 'enqueued chunk and branch2\'s chunk should have different buffers');

}, 'ReadableStream teeing with byte source: chunks should be cloned for each branch');

promise_test(async () => {

  let pullCount = 0;
  const rs = new ReadableStream({
    type: 'bytes',
    pull(c) {
      ++pullCount;
      if (pullCount === 1) {
        c.byobRequest.view[0] = 0x01;
        c.byobRequest.respond(1);
      }
    }
  });

  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader({ mode: 'byob' });
  const reader2 = branch2.getReader();
  const buffer = new Uint8Array([42, 42, 42]).buffer;

  {
    const result = await reader1.read(new Uint8Array(buffer, 0, 1));
    assert_equals(result.done, false, 'done');
    assert_typed_array_equals(result.value, new Uint8Array([0x01, 42, 42]).subarray(0, 1), 'value');
  }

  {
    const result = await reader2.read();
    assert_equals(result.done, false, 'done');
    assert_typed_array_equals(result.value, new Uint8Array([0x01]), 'value');
  }

}, 'ReadableStream teeing with byte source: chunks for BYOB requests from branch 1 should be cloned to branch 2');

promise_test(async t => {

  const theError = { name: 'boo!' };
  const rs = new ReadableStream({
    type: 'bytes',
    start(c) {
      c.enqueue(new Uint8Array([0x01]));
      c.enqueue(new Uint8Array([0x02]));
    },
    pull() {
      throw theError;
    }
  });

  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader({ mode: 'byob' });
  const reader2 = branch2.getReader({ mode: 'byob' });

  {
    const result = await reader1.read(new Uint8Array(1));
    assert_equals(result.done, false, 'first read from branch1 should not be done');
    assert_typed_array_equals(result.value, new Uint8Array([0x01]), 'first read from branch1');
  }

  {
    const result = await reader1.read(new Uint8Array(1));
    assert_equals(result.done, false, 'second read from branch1 should not be done');
    assert_typed_array_equals(result.value, new Uint8Array([0x02]), 'second read from branch1');
  }

  await promise_rejects_exactly(t, theError, reader1.read(new Uint8Array(1)));
  await promise_rejects_exactly(t, theError, reader2.read(new Uint8Array(1)));

  await Promise.all([
    promise_rejects_exactly(t, theError, reader1.closed),
    promise_rejects_exactly(t, theError, reader2.closed)
  ]);

}, 'ReadableStream teeing with byte source: errors in the source should propagate to both branches');

promise_test(async () => {

  const rs = new ReadableStream({
    type: 'bytes',
    start(c) {
      c.enqueue(new Uint8Array([0x01]));
      c.enqueue(new Uint8Array([0x02]));
      c.close();
    }
  });

  const [branch1, branch2] = rs.tee();
  branch1.cancel();

  const [chunks1, chunks2] = await Promise.all([readableStreamToArray(branch1), readableStreamToArray(branch2)]);
  assert_array_equals(chunks1, [], 'branch1 should have no chunks');
  assert_equals(chunks2.length, 2, 'branch2 should have two chunks');
  assert_typed_array_equals(chunks2[0], new Uint8Array([0x01]), 'first chunk from branch2');
  assert_typed_array_equals(chunks2[1], new Uint8Array([0x02]), 'second chunk from branch2');

}, 'ReadableStream teeing with byte source: canceling branch1 should not impact branch2');

promise_test(async () => {

  const rs = new ReadableStream({
    type: 'bytes',
    start(c) {
      c.enqueue(new Uint8Array([0x01]));
      c.enqueue(new Uint8Array([0x02]));
      c.close();
    }
  });

  const [branch1, branch2] = rs.tee();
  branch2.cancel();

  const [chunks1, chunks2] = await Promise.all([readableStreamToArray(branch1), readableStreamToArray(branch2)]);
  assert_equals(chunks1.length, 2, 'branch1 should have two chunks');
  assert_typed_array_equals(chunks1[0], new Uint8Array([0x01]), 'first chunk from branch1');
  assert_typed_array_equals(chunks1[1], new Uint8Array([0x02]), 'second chunk from branch1');
  assert_array_equals(chunks2, [], 'branch2 should have no chunks');

}, 'ReadableStream teeing with byte source: canceling branch2 should not impact branch1');

templatedRSTeeCancel('ReadableStream teeing with byte source', (extras) => {
  return new ReadableStream({ type: 'bytes', ...extras });
});

promise_test(async () => {

  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start(c) {
      controller = c;
    }
  });

  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader({ mode: 'byob' });
  const reader2 = branch2.getReader({ mode: 'byob' });

  const promise = Promise.all([reader1.closed, reader2.closed]);

  controller.close();

  // The branches are created with HWM 0, so we need to read from at least one of them
  // to observe the stream becoming closed.
  const read1 = await reader1.read(new Uint8Array(1));
  assert_equals(read1.done, true, 'first read from branch1 should be done');

  await promise;

}, 'ReadableStream teeing with byte source: closing the original should close the branches');

promise_test(async t => {

  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start(c) {
      controller = c;
    }
  });

  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader({ mode: 'byob' });
  const reader2 = branch2.getReader({ mode: 'byob' });

  const theError = { name: 'boo!' };
  const promise = Promise.all([
    promise_rejects_exactly(t, theError, reader1.closed),
    promise_rejects_exactly(t, theError, reader2.closed)
  ]);

  controller.error(theError);
  await promise;

}, 'ReadableStream teeing with byte source: erroring the original should immediately error the branches');

promise_test(async t => {

  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start(c) {
      controller = c;
    }
  });

  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader();
  const reader2 = branch2.getReader();

  const theError = { name: 'boo!' };
  const promise = Promise.all([
    promise_rejects_exactly(t, theError, reader1.read()),
    promise_rejects_exactly(t, theError, reader2.read())
  ]);

  controller.error(theError);
  await promise;

}, 'ReadableStream teeing with byte source: erroring the original should error pending reads from default reader');

promise_test(async t => {

  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start(c) {
      controller = c;
    }
  });

  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader({ mode: 'byob' });
  const reader2 = branch2.getReader({ mode: 'byob' });

  const theError = { name: 'boo!' };
  const promise = Promise.all([
    promise_rejects_exactly(t, theError, reader1.read(new Uint8Array(1))),
    promise_rejects_exactly(t, theError, reader2.read(new Uint8Array(1)))
  ]);

  controller.error(theError);
  await promise;

}, 'ReadableStream teeing with byte source: erroring the original should error pending reads from BYOB reader');

promise_test(async () => {

  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start(c) {
      controller = c;
    }
  });

  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader({ mode: 'byob' });
  const reader2 = branch2.getReader({ mode: 'byob' });
  const cancelPromise = reader2.cancel();

  controller.enqueue(new Uint8Array([0x01]));

  const read1 = await reader1.read(new Uint8Array(1));
  assert_equals(read1.done, false, 'first read() from branch1 should not be done');
  assert_typed_array_equals(read1.value, new Uint8Array([0x01]), 'first read() from branch1');

  controller.close();

  const read2 = await reader1.read(new Uint8Array(1));
  assert_equals(read2.done, true, 'second read() from branch1 should be done');

  await Promise.all([
    reader1.closed,
    cancelPromise
  ]);

}, 'ReadableStream teeing with byte source: canceling branch1 should finish when branch2 reads until end of stream');

promise_test(async t => {

  let controller;
  const theError = { name: 'boo!' };
  const rs = new ReadableStream({
    type: 'bytes',
    start(c) {
      controller = c;
    }
  });

  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader({ mode: 'byob' });
  const reader2 = branch2.getReader({ mode: 'byob' });
  const cancelPromise = reader2.cancel();

  controller.error(theError);

  await Promise.all([
    promise_rejects_exactly(t, theError, reader1.read(new Uint8Array(1))),
    cancelPromise
  ]);

}, 'ReadableStream teeing with byte source: canceling branch1 should finish when original stream errors');

promise_test(async () => {

  const rs = recordingReadableStream({ type: 'bytes' });

  // Create two branches, each with a HWM of 0. This should result in no chunks being pulled.
  rs.tee();

  await flushAsyncEvents();
  assert_array_equals(rs.events, [], 'pull should not be called');

}, 'ReadableStream teeing with byte source: should not pull any chunks if no branches are reading');

promise_test(async () => {

  const rs = recordingReadableStream({
    type: 'bytes',
    pull(controller) {
      controller.enqueue(new Uint8Array([0x01]));
    }
  });

  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));
  await Promise.all([
    reader1.read(new Uint8Array(1)),
    reader2.read(new Uint8Array(1))
  ]);
  assert_array_equals(rs.events, ['pull'], 'pull should be called once');

}, 'ReadableStream teeing with byte source: should only pull enough to fill the emptiest queue');

promise_test(async t => {

  const rs = recordingReadableStream({ type: 'bytes' });
  const theError = { name: 'boo!' };

  rs.controller.error(theError);

  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));

  await flushAsyncEvents();
  assert_array_equals(rs.events, [], 'pull should not be called');

  await Promise.all([
    promise_rejects_exactly(t, theError, reader1.closed),
    promise_rejects_exactly(t, theError, reader2.closed)
  ]);

}, 'ReadableStream teeing with byte source: should not pull when original is already errored');

for (const branch of [1, 2]) {
  promise_test(async t => {

    const rs = recordingReadableStream({ type: 'bytes' });
    const theError = { name: 'boo!' };

    const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));

    await flushAsyncEvents();
    assert_array_equals(rs.events, [], 'pull should not be called');

    const reader = (branch === 1) ? reader1 : reader2;
    const read1 = reader.read(new Uint8Array(1));

    await flushAsyncEvents();
    assert_array_equals(rs.events, ['pull'], 'pull should be called once');

    rs.controller.error(theError);

    await Promise.all([
      promise_rejects_exactly(t, theError, read1),
      promise_rejects_exactly(t, theError, reader1.closed),
      promise_rejects_exactly(t, theError, reader2.closed)
    ]);

    await flushAsyncEvents();
    assert_array_equals(rs.events, ['pull'], 'pull should be called once');

  }, `ReadableStream teeing with byte source: stops pulling when original stream errors while branch ${branch} is reading`);
}

promise_test(async t => {

  const rs = recordingReadableStream({ type: 'bytes' });
  const theError = { name: 'boo!' };

  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));

  await flushAsyncEvents();
  assert_array_equals(rs.events, [], 'pull should not be called');

  const read1 = reader1.read(new Uint8Array(1));
  const read2 = reader2.read(new Uint8Array(1));

  await flushAsyncEvents();
  assert_array_equals(rs.events, ['pull'], 'pull should be called once');

  rs.controller.error(theError);

  await Promise.all([
    promise_rejects_exactly(t, theError, read1),
    promise_rejects_exactly(t, theError, read2),
    promise_rejects_exactly(t, theError, reader1.closed),
    promise_rejects_exactly(t, theError, reader2.closed)
  ]);

  await flushAsyncEvents();
  assert_array_equals(rs.events, ['pull'], 'pull should be called once');

}, 'ReadableStream teeing with byte source: stops pulling when original stream errors while both branches are reading');

promise_test(async () => {

  const rs = recordingReadableStream({ type: 'bytes' });

  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));

  const read1 = reader1.read(new Uint8Array([0x11]));
  const read2 = reader2.read(new Uint8Array([0x22]));

  const cancel1 = reader1.cancel();
  await flushAsyncEvents();
  const cancel2 = reader2.cancel();

  const result1 = await read1;
  assert_object_equals(result1, { value: undefined, done: true });
  const result2 = await read2;
  assert_object_equals(result2, { value: undefined, done: true });

  await Promise.all([cancel1, cancel2]);

}, 'ReadableStream teeing with byte source: canceling both branches in sequence with delay');

promise_test(async t => {

  const theError = { name: 'boo!' };
  const rs = new ReadableStream({
    type: 'bytes',
    cancel() {
      throw theError;
    }
  });

  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));

  const read1 = reader1.read(new Uint8Array([0x11]));
  const read2 = reader2.read(new Uint8Array([0x22]));

  const cancel1 = reader1.cancel();
  await flushAsyncEvents();
  const cancel2 = reader2.cancel();

  const result1 = await read1;
  assert_object_equals(result1, { value: undefined, done: true });
  const result2 = await read2;
  assert_object_equals(result2, { value: undefined, done: true });

  await Promise.all([
    promise_rejects_exactly(t, theError, cancel1),
    promise_rejects_exactly(t, theError, cancel2)
  ]);

}, 'ReadableStream teeing with byte source: failing to cancel when canceling both branches in sequence with delay');

promise_test(async () => {

  let cancelResolve;
  const cancelCalled = new Promise((resolve) => {
    cancelResolve = resolve;
  });
  const rs = recordingReadableStream({
    type: 'bytes',
    cancel() {
      cancelResolve();
    }
  });

  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));

  const read1 = reader1.read(new Uint8Array([0x11]));
  await flushAsyncEvents();
  const read2 = reader2.read(new Uint8Array([0x22]));
  await flushAsyncEvents();

  // We are reading into branch1's buffer.
  const byobRequest1 = rs.controller.byobRequest;
  assert_not_equals(byobRequest1, null);
  assert_typed_array_equals(byobRequest1.view, new Uint8Array([0x11]), 'byobRequest1.view');

  // Cancelling branch1 should not affect the BYOB request.
  const cancel1 = reader1.cancel();
  const result1 = await read1;
  assert_equals(result1.done, true);
  assert_equals(result1.value, undefined);
  await flushAsyncEvents();
  const byobRequest2 = rs.controller.byobRequest;
  assert_typed_array_equals(byobRequest2.view, new Uint8Array([0x11]), 'byobRequest2.view');

  // Cancelling branch1 should invalidate the BYOB request.
  const cancel2 = reader2.cancel();
  await cancelCalled;
  const byobRequest3 = rs.controller.byobRequest;
  assert_equals(byobRequest3, null);
  const result2 = await read2;
  assert_equals(result2.done, true);
  assert_equals(result2.value, undefined);

  await Promise.all([cancel1, cancel2]);

}, 'ReadableStream teeing with byte source: read from branch1 and branch2, cancel branch1, cancel branch2');

promise_test(async () => {

  let cancelResolve;
  const cancelCalled = new Promise((resolve) => {
    cancelResolve = resolve;
  });
  const rs = recordingReadableStream({
    type: 'bytes',
    cancel() {
      cancelResolve();
    }
  });

  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));

  const read1 = reader1.read(new Uint8Array([0x11]));
  await flushAsyncEvents();
  const read2 = reader2.read(new Uint8Array([0x22]));
  await flushAsyncEvents();

  // We are reading into branch1's buffer.
  const byobRequest1 = rs.controller.byobRequest;
  assert_not_equals(byobRequest1, null);
  assert_typed_array_equals(byobRequest1.view, new Uint8Array([0x11]), 'byobRequest1.view');

  // Cancelling branch2 should not affect the BYOB request.
  const cancel2 = reader2.cancel();
  const result2 = await read2;
  assert_equals(result2.done, true);
  assert_equals(result2.value, undefined);
  await flushAsyncEvents();
  const byobRequest2 = rs.controller.byobRequest;
  assert_typed_array_equals(byobRequest2.view, new Uint8Array([0x11]), 'byobRequest2.view');

  // Cancelling branch1 should invalidate the BYOB request.
  const cancel1 = reader1.cancel();
  await cancelCalled;
  const byobRequest3 = rs.controller.byobRequest;
  assert_equals(byobRequest3, null);
  const result1 = await read1;
  assert_equals(result1.done, true);
  assert_equals(result1.value, undefined);

  await Promise.all([cancel1, cancel2]);

}, 'ReadableStream teeing with byte source: read from branch1 and branch2, cancel branch2, cancel branch1');

promise_test(async () => {

  const rs = recordingReadableStream({ type: 'bytes' });

  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));

  const read1 = reader1.read(new Uint8Array([0x11]));
  await flushAsyncEvents();
  const read2 = reader2.read(new Uint8Array([0x22]));
  await flushAsyncEvents();

  // We are reading into branch1's buffer.
  assert_typed_array_equals(rs.controller.byobRequest.view, new Uint8Array([0x11]), 'first byobRequest.view');

  // Cancelling branch2 should not affect the BYOB request.
  reader2.cancel();
  const result2 = await read2;
  assert_equals(result2.done, true);
  assert_equals(result2.value, undefined);
  await flushAsyncEvents();
  assert_typed_array_equals(rs.controller.byobRequest.view, new Uint8Array([0x11]), 'second byobRequest.view');

  // Respond to the BYOB request.
  rs.controller.byobRequest.view[0] = 0x33;
  rs.controller.byobRequest.respond(1);

  // branch1 should receive the read chunk.
  const result1 = await read1;
  assert_equals(result1.done, false);
  assert_typed_array_equals(result1.value, new Uint8Array([0x33]), 'first read() from branch1');

}, 'ReadableStream teeing with byte source: read from branch1 and branch2, cancel branch2, enqueue to branch1');

promise_test(async () => {

  const rs = recordingReadableStream({ type: 'bytes' });

  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));

  const read1 = reader1.read(new Uint8Array([0x11]));
  await flushAsyncEvents();
  const read2 = reader2.read(new Uint8Array([0x22]));
  await flushAsyncEvents();

  // We are reading into branch1's buffer.
  assert_typed_array_equals(rs.controller.byobRequest.view, new Uint8Array([0x11]), 'first byobRequest.view');

  // Cancelling branch1 should not affect the BYOB request.
  reader1.cancel();
  const result1 = await read1;
  assert_equals(result1.done, true);
  assert_equals(result1.value, undefined);
  await flushAsyncEvents();
  assert_typed_array_equals(rs.controller.byobRequest.view, new Uint8Array([0x11]), 'second byobRequest.view');

  // Respond to the BYOB request.
  rs.controller.byobRequest.view[0] = 0x33;
  rs.controller.byobRequest.respond(1);

  // branch2 should receive the read chunk.
  const result2 = await read2;
  assert_equals(result2.done, false);
  assert_typed_array_equals(result2.value, new Uint8Array([0x33]), 'first read() from branch2');

}, 'ReadableStream teeing with byte source: read from branch1 and branch2, cancel branch1, respond to branch2');

promise_test(async () => {

  let pullCount = 0;
  const byobRequestDefined = [];
  const rs = new ReadableStream({
    type: 'bytes',
    pull(c) {
      ++pullCount;
      byobRequestDefined.push(c.byobRequest !== null);
      c.enqueue(new Uint8Array([pullCount]));
    }
  });

  const [branch1, _] = rs.tee();
  const reader1 = branch1.getReader({ mode: 'byob' });

  const result1 = await reader1.read(new Uint8Array([0x11]));
  assert_equals(result1.done, false, 'first read should not be done');
  assert_typed_array_equals(result1.value, new Uint8Array([0x1]), 'first read');
  assert_equals(pullCount, 1, 'pull() should be called once');
  assert_equals(byobRequestDefined[0], true, 'should have created a BYOB request for first read');

  reader1.releaseLock();
  const reader2 = branch1.getReader();

  const result2 = await reader2.read();
  assert_equals(result2.done, false, 'second read should not be done');
  assert_typed_array_equals(result2.value, new Uint8Array([0x2]), 'second read');
  assert_equals(pullCount, 2, 'pull() should be called twice');
  assert_equals(byobRequestDefined[1], false, 'should not have created a BYOB request for second read');

}, 'ReadableStream teeing with byte source: pull with BYOB reader, then pull with default reader');

promise_test(async () => {

  let pullCount = 0;
  const byobRequestDefined = [];
  const rs = new ReadableStream({
    type: 'bytes',
    pull(c) {
      ++pullCount;
      byobRequestDefined.push(c.byobRequest !== null);
      c.enqueue(new Uint8Array([pullCount]));
    }
  });

  const [branch1, _] = rs.tee();
  const reader1 = branch1.getReader();

  const result1 = await reader1.read();
  assert_equals(result1.done, false, 'first read should not be done');
  assert_typed_array_equals(result1.value, new Uint8Array([0x1]), 'first read');
  assert_equals(pullCount, 1, 'pull() should be called once');
  assert_equals(byobRequestDefined[0], false, 'should not have created a BYOB request for first read');

  reader1.releaseLock();
  const reader2 = branch1.getReader({ mode: 'byob' });

  const result2 = await reader2.read(new Uint8Array([0x22]));
  assert_equals(result2.done, false, 'second read should not be done');
  assert_typed_array_equals(result2.value, new Uint8Array([0x2]), 'second read');
  assert_equals(pullCount, 2, 'pull() should be called twice');
  assert_equals(byobRequestDefined[1], true, 'should have created a BYOB request for second read');

}, 'ReadableStream teeing with byte source: pull with default reader, then pull with BYOB reader');

promise_test(async () => {

  const rs = recordingReadableStream({
    type: 'bytes'
  });
  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));

  // Wait for each branch's start() promise to resolve.
  await flushAsyncEvents();

  const read2 = reader2.read(new Uint8Array([0x22]));
  const read1 = reader1.read(new Uint8Array([0x11]));
  await flushAsyncEvents();

  // branch2 should provide the BYOB request.
  const byobRequest = rs.controller.byobRequest;
  assert_typed_array_equals(byobRequest.view, new Uint8Array([0x22]), 'first BYOB request');
  byobRequest.view[0] = 0x01;
  byobRequest.respond(1);

  const result1 = await read1;
  assert_equals(result1.done, false, 'first read should not be done');
  assert_typed_array_equals(result1.value, new Uint8Array([0x1]), 'first read');

  const result2 = await read2;
  assert_equals(result2.done, false, 'second read should not be done');
  assert_typed_array_equals(result2.value, new Uint8Array([0x1]), 'second read');

}, 'ReadableStream teeing with byte source: read from branch2, then read from branch1');

promise_test(async () => {

  const rs = recordingReadableStream({ type: 'bytes' });
  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader();
  const reader2 = branch2.getReader({ mode: 'byob' });
  await flushAsyncEvents();

  const read1 = reader1.read();
  const read2 = reader2.read(new Uint8Array([0x22]));
  await flushAsyncEvents();

  // There should be no BYOB request.
  assert_equals(rs.controller.byobRequest, null, 'first BYOB request');

  // Close the stream.
  rs.controller.close();

  const result1 = await read1;
  assert_equals(result1.done, true, 'read from branch1 should be done');
  assert_equals(result1.value, undefined, 'read from branch1');

  // branch2 should get its buffer back.
  const result2 = await read2;
  assert_equals(result2.done, true, 'read from branch2 should be done');
  assert_typed_array_equals(result2.value, new Uint8Array([0x22]).subarray(0, 0), 'read from branch2');

}, 'ReadableStream teeing with byte source: read from branch1 with default reader, then close while branch2 has pending BYOB read');

promise_test(async () => {

  const rs = recordingReadableStream({ type: 'bytes' });
  const [branch1, branch2] = rs.tee();
  const reader1 = branch1.getReader({ mode: 'byob' });
  const reader2 = branch2.getReader();
  await flushAsyncEvents();

  const read2 = reader2.read();
  const read1 = reader1.read(new Uint8Array([0x11]));
  await flushAsyncEvents();

  // There should be no BYOB request.
  assert_equals(rs.controller.byobRequest, null, 'first BYOB request');

  // Close the stream.
  rs.controller.close();

  const result2 = await read2;
  assert_equals(result2.done, true, 'read from branch2 should be done');
  assert_equals(result2.value, undefined, 'read from branch2');

  // branch1 should get its buffer back.
  const result1 = await read1;
  assert_equals(result1.done, true, 'read from branch1 should be done');
  assert_typed_array_equals(result1.value, new Uint8Array([0x11]).subarray(0, 0), 'read from branch1');

}, 'ReadableStream teeing with byte source: read from branch2 with default reader, then close while branch1 has pending BYOB read');

promise_test(async () => {

  const rs = recordingReadableStream({ type: 'bytes' });
  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));
  await flushAsyncEvents();

  const read1 = reader1.read(new Uint8Array([0x11]));
  const read2 = reader2.read(new Uint8Array([0x22]));
  await flushAsyncEvents();

  // branch1 should provide the BYOB request.
  const byobRequest = rs.controller.byobRequest;
  assert_typed_array_equals(byobRequest.view, new Uint8Array([0x11]), 'first BYOB request');

  // Close the stream.
  rs.controller.close();
  byobRequest.respond(0);

  // Both branches should get their buffers back.
  const result1 = await read1;
  assert_equals(result1.done, true, 'first read should be done');
  assert_typed_array_equals(result1.value, new Uint8Array([0x11]).subarray(0, 0), 'first read');

  const result2 = await read2;
  assert_equals(result2.done, true, 'second read should be done');
  assert_typed_array_equals(result2.value, new Uint8Array([0x22]).subarray(0, 0), 'second read');

}, 'ReadableStream teeing with byte source: close when both branches have pending BYOB reads');

promise_test(async () => {

  const rs = recordingReadableStream({ type: 'bytes' });

  const [reader1, reader2] = rs.tee().map(branch => branch.getReader());
  const branch1Reads = [reader1.read(), reader1.read()];
  const branch2Reads = [reader2.read(), reader2.read()];

  await flushAsyncEvents();
  rs.controller.enqueue(new Uint8Array([0x11]));
  rs.controller.close();

  const result1 = await branch1Reads[0];
  assert_equals(result1.done, false, 'first read() from branch1 should be not done');
  assert_typed_array_equals(result1.value, new Uint8Array([0x11]), 'first chunk from branch1 should be correct');
  const result2 = await branch2Reads[0];
  assert_equals(result2.done, false, 'first read() from branch2 should be not done');
  assert_typed_array_equals(result2.value, new Uint8Array([0x11]), 'first chunk from branch2 should be correct');

  assert_object_equals(await branch1Reads[1], { value: undefined, done: true }, 'second read() from branch1 should be done');
  assert_object_equals(await branch2Reads[1], { value: undefined, done: true }, 'second read() from branch2 should be done');

}, 'ReadableStream teeing with byte source: enqueue() and close() while both branches are pulling');

promise_test(async () => {

  const rs = recordingReadableStream({ type: 'bytes' });

  const [reader1, reader2] = rs.tee().map(branch => branch.getReader({ mode: 'byob' }));
  const branch1Reads = [reader1.read(new Uint8Array(1)), reader1.read(new Uint8Array(1))];
  const branch2Reads = [reader2.read(new Uint8Array(1)), reader2.read(new Uint8Array(1))];

  await flushAsyncEvents();
  rs.controller.byobRequest.view[0] = 0x11;
  rs.controller.byobRequest.respond(1);
  rs.controller.close();

  const result1 = await branch1Reads[0];
  assert_equals(result1.done, false, 'first read() from branch1 should be not done');
  assert_typed_array_equals(result1.value, new Uint8Array([0x11]), 'first chunk from branch1 should be correct');
  const result2 = await branch2Reads[0];
  assert_equals(result2.done, false, 'first read() from branch2 should be not done');
  assert_typed_array_equals(result2.value, new Uint8Array([0x11]), 'first chunk from branch2 should be correct');

  const result3 = await branch1Reads[1];
  assert_equals(result3.done, true, 'second read() from branch1 should be done');
  assert_typed_array_equals(result3.value, new Uint8Array([0]).subarray(0, 0), 'second chunk from branch1 should be correct');
  const result4 = await branch2Reads[1];
  assert_equals(result4.done, true, 'second read() from branch2 should be done');
  assert_typed_array_equals(result4.value, new Uint8Array([0]).subarray(0, 0), 'second chunk from branch2 should be correct');

}, 'ReadableStream teeing with byte source: respond() and close() while both branches are pulling');
