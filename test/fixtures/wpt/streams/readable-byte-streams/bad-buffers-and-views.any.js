// META: global=window,worker,shadowrealm
'use strict';

promise_test(() => {
  const stream = new ReadableStream({
    start(c) {
      c.close();
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });
  const view = new Uint8Array([1, 2, 3]);
  return reader.read(view).then(({ value, done }) => {
    // Sanity checks
    assert_true(value instanceof Uint8Array, 'The value read must be a Uint8Array');
    assert_not_equals(value, view, 'The value read must not be the *same* Uint8Array');
    assert_array_equals(value, [], 'The value read must be an empty Uint8Array, since the stream is closed');
    assert_true(done, 'done must be true, since the stream is closed');

    // The important assertions
    assert_not_equals(value.buffer, view.buffer, 'a different ArrayBuffer must underlie the value');
    assert_equals(view.buffer.byteLength, 0, 'the original buffer must be detached');
  });
}, 'ReadableStream with byte source: read()ing from a closed stream still transfers the buffer');

promise_test(() => {
  const stream = new ReadableStream({
    start(c) {
      c.enqueue(new Uint8Array([1, 2, 3]));
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });
  const view = new Uint8Array([4, 5, 6]);
  return reader.read(view).then(({ value, done }) => {
    // Sanity checks
    assert_true(value instanceof Uint8Array, 'The value read must be a Uint8Array');
    assert_not_equals(value, view, 'The value read must not be the *same* Uint8Array');
    assert_array_equals(value, [1, 2, 3], 'The value read must be the enqueued Uint8Array, not the original values');
    assert_false(done, 'done must be false, since the stream is not closed');

    // The important assertions
    assert_not_equals(value.buffer, view.buffer, 'a different ArrayBuffer must underlie the value');
    assert_equals(view.buffer.byteLength, 0, 'the original buffer must be detached');
  });
}, 'ReadableStream with byte source: read()ing from a stream with queued chunks still transfers the buffer');

test(() => {
  new ReadableStream({
    start(c) {
      const view = new Uint8Array([1, 2, 3]);
      c.enqueue(view);
      assert_throws_js(TypeError, () => c.enqueue(view));
    },
    type: 'bytes'
  });
}, 'ReadableStream with byte source: enqueuing an already-detached buffer throws');

test(() => {
  new ReadableStream({
    start(c) {
      const view = new Uint8Array([]);
      assert_throws_js(TypeError, () => c.enqueue(view));
    },
    type: 'bytes'
  });
}, 'ReadableStream with byte source: enqueuing a zero-length buffer throws');

test(() => {
  new ReadableStream({
    start(c) {
      const view = new Uint8Array(new ArrayBuffer(10), 0, 0);
      assert_throws_js(TypeError, () => c.enqueue(view));
    },
    type: 'bytes'
  });
}, 'ReadableStream with byte source: enqueuing a zero-length view on a non-zero-length buffer throws');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      c.enqueue(new Uint8Array([1, 2, 3]));
    },
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  const view = new Uint8Array([4, 5, 6]);
  return reader.read(view).then(() => {
    // view is now detached
    return promise_rejects_js(t, TypeError, reader.read(view));
  });
}, 'ReadableStream with byte source: reading into an already-detached buffer rejects');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      c.enqueue(new Uint8Array([1, 2, 3]));
    },
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  const view = new Uint8Array();
  return promise_rejects_js(t, TypeError, reader.read(view));
}, 'ReadableStream with byte source: reading into a zero-length buffer rejects');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      c.enqueue(new Uint8Array([1, 2, 3]));
    },
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  const view = new Uint8Array(new ArrayBuffer(10), 0, 0);
  return promise_rejects_js(t, TypeError, reader.read(view));
}, 'ReadableStream with byte source: reading into a zero-length view on a non-zero-length buffer rejects');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      // Detach it by reading into it
      reader.read(c.byobRequest.view);

      assert_throws_js(TypeError, () => c.byobRequest.respond(1),
        'respond() must throw if the corresponding view has become detached');
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respond() throws if the BYOB request\'s buffer has been detached (in the ' +
   'readable state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      c.close();

      // Detach it by reading into it
      reader.read(c.byobRequest.view);

      assert_throws_js(TypeError, () => c.byobRequest.respond(0),
        'respond() must throw if the corresponding view has become detached');
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respond() throws if the BYOB request\'s buffer has been detached (in the ' +
   'closed state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      // Detach it by reading into it
      const view = new Uint8Array([1, 2, 3]);
      reader.read(view);

      assert_throws_js(TypeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view\'s buffer has been detached ' +
    '(in the readable state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      const view = new Uint8Array();

      assert_throws_js(TypeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view\'s buffer is zero-length ' +
    '(in the readable state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      const view = new Uint8Array(c.byobRequest.view.buffer, 0, 0);

      assert_throws_js(TypeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view is zero-length on a ' +
    'non-zero-length buffer (in the readable state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      const view = c.byobRequest.view.subarray(1, 2);

      assert_throws_js(RangeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view has a different offset ' +
   '(in the readable state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      c.close();

      const view = c.byobRequest.view.subarray(1, 1);

      assert_throws_js(RangeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view has a different offset ' +
   '(in the closed state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      const view = new Uint8Array(new ArrayBuffer(10), 0, 3);

      assert_throws_js(RangeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view\'s buffer has a ' +
   'different length (in the readable state)');

async_test(t => {
  // Tests https://github.com/nodejs/node/issues/41886
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      const view = new Uint8Array(new ArrayBuffer(11), 0, 3);

      assert_throws_js(RangeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes',
    autoAllocateChunkSize: 10
  });
  const reader = stream.getReader();

  reader.read();
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view\'s buffer has a ' +
   'different length (autoAllocateChunkSize)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      const view = new Uint8Array(c.byobRequest.view.buffer, 0, 4);
      view[0] = 20;
      view[1] = 21;
      view[2] = 22;
      view[3] = 23;

      assert_throws_js(RangeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  const buffer = new ArrayBuffer(10);
  const view = new Uint8Array(buffer, 0, 3);
  view[0] = 10;
  view[1] = 11;
  view[2] = 12;
  reader.read(view);
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view has a larger length ' +
   '(in the readable state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      c.close();

      // Detach it by reading into it
      const view = new Uint8Array([1, 2, 3]);
      reader.read(view);

      assert_throws_js(TypeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view\'s buffer has been detached ' +
   '(in the closed state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      const view = new Uint8Array();

      c.close();

      assert_throws_js(RangeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view\'s buffer is zero-length ' +
   '(in the closed state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      const view = new Uint8Array(c.byobRequest.view.buffer, 0, 1);

      c.close();

      assert_throws_js(TypeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view is non-zero-length ' +
   '(in the closed state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      const view = new Uint8Array(new ArrayBuffer(10), 0, 0);

      c.close();

      assert_throws_js(RangeError, () => c.byobRequest.respondWithNewView(view));
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: respondWithNewView() throws if the supplied view\'s buffer has a ' +
   'different length (in the closed state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      // Detach it by reading into it
      reader.read(c.byobRequest.view);

      assert_throws_js(TypeError, () => c.enqueue(new Uint8Array([1])),
        'enqueue() must throw if the BYOB request\'s buffer has become detached');
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: enqueue() throws if the BYOB request\'s buffer has been detached (in the ' +
  'readable state)');

async_test(t => {
  const stream = new ReadableStream({
    pull: t.step_func_done(c => {
      c.close();

      // Detach it by reading into it
      reader.read(c.byobRequest.view);

      assert_throws_js(TypeError, () => c.enqueue(new Uint8Array([1])),
        'enqueue() must throw if the BYOB request\'s buffer has become detached');
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new Uint8Array([4, 5, 6]));
}, 'ReadableStream with byte source: enqueue() throws if the BYOB request\'s buffer has been detached (in the ' +
  'closed state)');
