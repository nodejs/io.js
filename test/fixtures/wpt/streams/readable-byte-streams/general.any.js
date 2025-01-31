// META: global=window,worker,shadowrealm
// META: script=../resources/rs-utils.js
// META: script=../resources/test-utils.js
'use strict';

const error1 = new Error('error1');
error1.name = 'error1';

test(() => {
  assert_throws_js(TypeError, () => new ReadableStream().getReader({ mode: 'byob' }));
}, 'getReader({mode: "byob"}) throws on non-bytes streams');


test(() => {
  // Constructing ReadableStream with an empty underlying byte source object as parameter shouldn't throw.
  new ReadableStream({ type: 'bytes' }).getReader({ mode: 'byob' });
  // Constructor must perform ToString(type).
  new ReadableStream({ type: { toString() {return 'bytes';} } })
    .getReader({ mode: 'byob' });
  new ReadableStream({ type: { toString: null, valueOf() {return 'bytes';} } })
    .getReader({ mode: 'byob' });
}, 'ReadableStream with byte source can be constructed with no errors');

test(() => {
  const ReadableStreamBYOBReader = new ReadableStream({ type: 'bytes' }).getReader({ mode: 'byob' }).constructor;
  const rs = new ReadableStream({ type: 'bytes' });

  let reader = rs.getReader({ mode: { toString() { return 'byob'; } } });
  assert_true(reader instanceof ReadableStreamBYOBReader, 'must give a BYOB reader');
  reader.releaseLock();

  reader = rs.getReader({ mode: { toString: null, valueOf() {return 'byob';} } });
  assert_true(reader instanceof ReadableStreamBYOBReader, 'must give a BYOB reader');
  reader.releaseLock();

  reader = rs.getReader({ mode: 'byob', notmode: 'ignored' });
  assert_true(reader instanceof ReadableStreamBYOBReader, 'must give a BYOB reader');
}, 'getReader({mode}) must perform ToString()');

promise_test(() => {
  let startCalled = false;
  let startCalledBeforePull = false;
  let desiredSize;
  let controller;

  let resolveTestPromise;
  const testPromise = new Promise(resolve => {
    resolveTestPromise = resolve;
  });

  new ReadableStream({
    start(c) {
      controller = c;
      startCalled = true;
    },
    pull() {
      startCalledBeforePull = startCalled;
      desiredSize = controller.desiredSize;
      resolveTestPromise();
    },
    type: 'bytes'
  }, {
    highWaterMark: 256
  });

  return testPromise.then(() => {
    assert_true(startCalledBeforePull, 'start should be called before pull');
    assert_equals(desiredSize, 256, 'desiredSize should equal highWaterMark');
  });

}, 'ReadableStream with byte source: Construct and expect start and pull being called');

promise_test(() => {
  let pullCount = 0;
  let checkedNoPull = false;

  let resolveTestPromise;
  const testPromise = new Promise(resolve => {
    resolveTestPromise = resolve;
  });
  let resolveStartPromise;

  new ReadableStream({
    start() {
      return new Promise(resolve => {
        resolveStartPromise = resolve;
      });
    },
    pull() {
      if (checkedNoPull) {
        resolveTestPromise();
      }

      ++pullCount;
    },
    type: 'bytes'
  }, {
    highWaterMark: 256
  });

  Promise.resolve().then(() => {
    assert_equals(pullCount, 0);
    checkedNoPull = true;
    resolveStartPromise();
  });

  return testPromise;

}, 'ReadableStream with byte source: No automatic pull call if start doesn\'t finish');

test(() => {
  assert_throws_js(Error, () => new ReadableStream({ start() { throw new Error(); }, type:'bytes' }),
      'start() can throw an exception with type: bytes');
}, 'ReadableStream with byte source: start() throws an exception');

promise_test(t => {
  new ReadableStream({
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  }, {
    highWaterMark: 0
  });

  return Promise.resolve();
}, 'ReadableStream with byte source: Construct with highWaterMark of 0');

test(() => {
  new ReadableStream({
    start(c) {
      assert_equals(c.desiredSize, 10, 'desiredSize must start at the highWaterMark');
      c.close();
      assert_equals(c.desiredSize, 0, 'after closing, desiredSize must be 0');
    },
    type: 'bytes'
  }, {
    highWaterMark: 10
  });
}, 'ReadableStream with byte source: desiredSize when closed');

test(() => {
  new ReadableStream({
    start(c) {
      assert_equals(c.desiredSize, 10, 'desiredSize must start at the highWaterMark');
      c.error();
      assert_equals(c.desiredSize, null, 'after erroring, desiredSize must be null');
    },
    type: 'bytes'
  }, {
    highWaterMark: 10
  });
}, 'ReadableStream with byte source: desiredSize when errored');

promise_test(t => {
  const stream = new ReadableStream({
    type: 'bytes'
  });

  const reader = stream.getReader();
  reader.releaseLock();

  return promise_rejects_js(t, TypeError, reader.closed, 'closed must reject');
}, 'ReadableStream with byte source: getReader(), then releaseLock()');

promise_test(t => {
  const stream = new ReadableStream({
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });
  reader.releaseLock();

  return promise_rejects_js(t, TypeError, reader.closed, 'closed must reject');
}, 'ReadableStream with byte source: getReader() with mode set to byob, then releaseLock()');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      c.close();
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader();

  return reader.closed.then(() => {
    assert_throws_js(TypeError, () => stream.getReader(), 'getReader() must throw');
  });
}, 'ReadableStream with byte source: Test that closing a stream does not release a reader automatically');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      c.close();
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.closed.then(() => {
    assert_throws_js(TypeError, () => stream.getReader({ mode: 'byob' }), 'getReader() must throw');
  });
}, 'ReadableStream with byte source: Test that closing a stream does not release a BYOB reader automatically');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      c.error(error1);
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader();

  return promise_rejects_exactly(t, error1, reader.closed, 'closed must reject').then(() => {
    assert_throws_js(TypeError, () => stream.getReader(), 'getReader() must throw');
  });
}, 'ReadableStream with byte source: Test that erroring a stream does not release a reader automatically');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      c.error(error1);
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return promise_rejects_exactly(t, error1, reader.closed, 'closed must reject').then(() => {
    assert_throws_js(TypeError, () => stream.getReader({ mode: 'byob' }), 'getReader() must throw');
  });
}, 'ReadableStream with byte source: Test that erroring a stream does not release a BYOB reader automatically');

promise_test(async t => {
  const stream = new ReadableStream({
    type: 'bytes'
  });

  const reader = stream.getReader();
  const read = reader.read();
  reader.releaseLock();
  await promise_rejects_js(t, TypeError, read, 'pending read must reject');
}, 'ReadableStream with byte source: releaseLock() on ReadableStreamDefaultReader must reject pending read()');

promise_test(async t => {
  const stream = new ReadableStream({
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });
  const read = reader.read(new Uint8Array(1));
  reader.releaseLock();
  await promise_rejects_js(t, TypeError, read, 'pending read must reject');
}, 'ReadableStream with byte source: releaseLock() on ReadableStreamBYOBReader must reject pending read()');

promise_test(() => {
  let pullCount = 0;

  const stream = new ReadableStream({
    pull() {
      ++pullCount;
    },
    type: 'bytes'
  }, {
    highWaterMark: 8
  });

  stream.getReader();

  assert_equals(pullCount, 0, 'No pull as start() just finished and is not yet reflected to the state of the stream');

  return Promise.resolve().then(() => {
    assert_equals(pullCount, 1, 'pull must be invoked');
  });
}, 'ReadableStream with byte source: Automatic pull() after start()');

promise_test(() => {
  let pullCount = 0;

  const stream = new ReadableStream({
    pull() {
      ++pullCount;
    },
    type: 'bytes'
  }, {
    highWaterMark: 0
  });

  const reader = stream.getReader();
  reader.read();

  assert_equals(pullCount, 0, 'No pull as start() just finished and is not yet reflected to the state of the stream');

  return Promise.resolve().then(() => {
    assert_equals(pullCount, 1, 'pull must be invoked');
  });
}, 'ReadableStream with byte source: Automatic pull() after start() and read()');

// View buffers are detached after pull() returns, so record the information at the time that pull() was called.
function extractViewInfo(view) {
  return {
    constructor: view.constructor,
    bufferByteLength: view.buffer.byteLength,
    byteOffset: view.byteOffset,
    byteLength: view.byteLength
  };
}

promise_test(() => {
  let pullCount = 0;
  let controller;
  const byobRequests = [];

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      const byobRequest = controller.byobRequest;
      const view = byobRequest.view;
      byobRequests[pullCount] = {
        nonNull: byobRequest !== null,
        viewNonNull: view !== null,
        viewInfo: extractViewInfo(view)
      };
      if (pullCount === 0) {
        view[0] = 0x01;
        byobRequest.respond(1);
      } else if (pullCount === 1) {
        view[0] = 0x02;
        view[1] = 0x03;
        byobRequest.respond(2);
      }

      ++pullCount;
    },
    type: 'bytes',
    autoAllocateChunkSize: 16
  }, {
    highWaterMark: 0
  });

  const reader = stream.getReader();
  const p0 = reader.read();
  const p1 = reader.read();

  assert_equals(pullCount, 0, 'No pull() as start() just finished and is not yet reflected to the state of the stream');

  return Promise.resolve().then(() => {
    assert_equals(pullCount, 1, 'pull() must have been invoked once');
    const byobRequest = byobRequests[0];
    assert_true(byobRequest.nonNull, 'first byobRequest must not be null');
    assert_true(byobRequest.viewNonNull, 'first byobRequest.view must not be null');
    const viewInfo = byobRequest.viewInfo;
    assert_equals(viewInfo.constructor, Uint8Array, 'first view.constructor should be Uint8Array');
    assert_equals(viewInfo.bufferByteLength, 16, 'first view.buffer.byteLength should be 16');
    assert_equals(viewInfo.byteOffset, 0, 'first view.byteOffset should be 0');
    assert_equals(viewInfo.byteLength, 16, 'first view.byteLength should be 16');

    return p0;
  }).then(result => {
    assert_equals(pullCount, 2, 'pull() must have been invoked twice');
    const value = result.value;
    assert_not_equals(value, undefined, 'first read should have a value');
    assert_equals(value.constructor, Uint8Array, 'first value should be a Uint8Array');
    assert_equals(value.buffer.byteLength, 16, 'first value.buffer.byteLength should be 16');
    assert_equals(value.byteOffset, 0, 'first value.byteOffset should be 0');
    assert_equals(value.byteLength, 1, 'first value.byteLength should be 1');
    assert_equals(value[0], 0x01, 'first value[0] should be 0x01');
    const byobRequest = byobRequests[1];
    assert_true(byobRequest.nonNull, 'second byobRequest must not be null');
    assert_true(byobRequest.viewNonNull, 'second byobRequest.view must not be null');
    const viewInfo = byobRequest.viewInfo;
    assert_equals(viewInfo.constructor, Uint8Array, 'second view.constructor should be Uint8Array');
    assert_equals(viewInfo.bufferByteLength, 16, 'second view.buffer.byteLength should be 16');
    assert_equals(viewInfo.byteOffset, 0, 'second view.byteOffset should be 0');
    assert_equals(viewInfo.byteLength, 16, 'second view.byteLength should be 16');

    return p1;
  }).then(result => {
    assert_equals(pullCount, 2, 'pull() should only be invoked twice');
    const value = result.value;
    assert_not_equals(value, undefined, 'second read should have a value');
    assert_equals(value.constructor, Uint8Array, 'second value should be a Uint8Array');
    assert_equals(value.buffer.byteLength, 16, 'second value.buffer.byteLength should be 16');
    assert_equals(value.byteOffset, 0, 'second value.byteOffset should be 0');
    assert_equals(value.byteLength, 2, 'second value.byteLength should be 2');
    assert_equals(value[0], 0x02, 'second value[0] should be 0x02');
    assert_equals(value[1], 0x03, 'second value[1] should be 0x03');
  });
}, 'ReadableStream with byte source: autoAllocateChunkSize');

promise_test(() => {
  let pullCount = 0;
  let controller;
  const byobRequests = [];

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      const byobRequest = controller.byobRequest;
      const view = byobRequest.view;
      byobRequests[pullCount] = {
        nonNull: byobRequest !== null,
        viewNonNull: view !== null,
        viewInfo: extractViewInfo(view)
      };
      if (pullCount === 0) {
        view[0] = 0x01;
        byobRequest.respond(1);
      } else if (pullCount === 1) {
        view[0] = 0x02;
        view[1] = 0x03;
        byobRequest.respond(2);
      }

      ++pullCount;
    },
    type: 'bytes',
    autoAllocateChunkSize: 16
  }, {
    highWaterMark: 0
  });

  const reader = stream.getReader();
  return reader.read().then(result => {
    const value = result.value;
    assert_not_equals(value, undefined, 'first read should have a value');
    assert_equals(value.constructor, Uint8Array, 'first value should be a Uint8Array');
    assert_equals(value.buffer.byteLength, 16, 'first value.buffer.byteLength should be 16');
    assert_equals(value.byteOffset, 0, 'first value.byteOffset should be 0');
    assert_equals(value.byteLength, 1, 'first value.byteLength should be 1');
    assert_equals(value[0], 0x01, 'first value[0] should be 0x01');
    const byobRequest = byobRequests[0];
    assert_true(byobRequest.nonNull, 'first byobRequest must not be null');
    assert_true(byobRequest.viewNonNull, 'first byobRequest.view must not be null');
    const viewInfo = byobRequest.viewInfo;
    assert_equals(viewInfo.constructor, Uint8Array, 'first view.constructor should be Uint8Array');
    assert_equals(viewInfo.bufferByteLength, 16, 'first view.buffer.byteLength should be 16');
    assert_equals(viewInfo.byteOffset, 0, 'first view.byteOffset should be 0');
    assert_equals(viewInfo.byteLength, 16, 'first view.byteLength should be 16');

    reader.releaseLock();
    const byobReader = stream.getReader({ mode: 'byob' });
    return byobReader.read(new Uint8Array(32));
  }).then(result => {
    const value = result.value;
    assert_not_equals(value, undefined, 'second read should have a value');
    assert_equals(value.constructor, Uint8Array, 'second value should be a Uint8Array');
    assert_equals(value.buffer.byteLength, 32, 'second value.buffer.byteLength should be 32');
    assert_equals(value.byteOffset, 0, 'second value.byteOffset should be 0');
    assert_equals(value.byteLength, 2, 'second value.byteLength should be 2');
    assert_equals(value[0], 0x02, 'second value[0] should be 0x02');
    assert_equals(value[1], 0x03, 'second value[1] should be 0x03');
    const byobRequest = byobRequests[1];
    assert_true(byobRequest.nonNull, 'second byobRequest must not be null');
    assert_true(byobRequest.viewNonNull, 'second byobRequest.view must not be null');
    const viewInfo = byobRequest.viewInfo;
    assert_equals(viewInfo.constructor, Uint8Array, 'second view.constructor should be Uint8Array');
    assert_equals(viewInfo.bufferByteLength, 32, 'second view.buffer.byteLength should be 32');
    assert_equals(viewInfo.byteOffset, 0, 'second view.byteOffset should be 0');
    assert_equals(viewInfo.byteLength, 32, 'second view.byteLength should be 32');
    assert_equals(pullCount, 2, 'pullCount should be 2');
  });
}, 'ReadableStream with byte source: Mix of auto allocate and BYOB');

promise_test(() => {
  let pullCount = 0;

  const stream = new ReadableStream({
    pull() {
      ++pullCount;
    },
    type: 'bytes'
  }, {
    highWaterMark: 0
  });

  const reader = stream.getReader();
  reader.read(new Uint8Array(8));

  assert_equals(pullCount, 0, 'No pull as start() just finished and is not yet reflected to the state of the stream');

  return Promise.resolve().then(() => {
    assert_equals(pullCount, 1, 'pull must be invoked');
  });
}, 'ReadableStream with byte source: Automatic pull() after start() and read(view)');

promise_test(() => {
  let pullCount = 0;

  let controller;
  let desiredSizeInStart;
  let desiredSizeInPull;

  const stream = new ReadableStream({
    start(c) {
      c.enqueue(new Uint8Array(16));
      desiredSizeInStart = c.desiredSize;
      controller = c;
    },
    pull() {
      ++pullCount;

      if (pullCount === 1) {
        desiredSizeInPull = controller.desiredSize;
      }
    },
    type: 'bytes'
  }, {
    highWaterMark: 8
  });

  return Promise.resolve().then(() => {
    assert_equals(pullCount, 0, 'No pull as the queue was filled by start()');
    assert_equals(desiredSizeInStart, -8, 'desiredSize after enqueue() in start()');

    const reader = stream.getReader();

    const promise = reader.read();
    assert_equals(pullCount, 1, 'The first pull() should be made on read()');
    assert_equals(desiredSizeInPull, 8, 'desiredSize in pull()');

    return promise.then(result => {
      assert_false(result.done, 'result.done');

      const view = result.value;
      assert_equals(view.constructor, Uint8Array, 'view.constructor');
      assert_equals(view.buffer.byteLength, 16, 'view.buffer');
      assert_equals(view.byteOffset, 0, 'view.byteOffset');
      assert_equals(view.byteLength, 16, 'view.byteLength');
    });
  });
}, 'ReadableStream with byte source: enqueue(), getReader(), then read()');

promise_test(() => {
  let controller;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    type: 'bytes'
  });

  const reader = stream.getReader();

  const promise = reader.read().then(result => {
    assert_false(result.done);

    const view = result.value;
    assert_equals(view.constructor, Uint8Array);
    assert_equals(view.buffer.byteLength, 1);
    assert_equals(view.byteOffset, 0);
    assert_equals(view.byteLength, 1);
  });

  controller.enqueue(new Uint8Array(1));

  return promise;
}, 'ReadableStream with byte source: Push source that doesn\'t understand pull signal');

test(() => {
  assert_throws_js(TypeError, () => new ReadableStream({
    pull: 'foo',
    type: 'bytes'
  }), 'constructor should throw');
}, 'ReadableStream with byte source: pull() function is not callable');

promise_test(() => {
  const stream = new ReadableStream({
    start(c) {
      c.enqueue(new Uint16Array(16));
    },
    type: 'bytes'
  });

  const reader = stream.getReader();

  return reader.read().then(result => {
    assert_false(result.done);

    const view = result.value;
    assert_equals(view.constructor, Uint8Array);
    assert_equals(view.buffer.byteLength, 32);
    assert_equals(view.byteOffset, 0);
    assert_equals(view.byteLength, 32);
  });
}, 'ReadableStream with byte source: enqueue() with Uint16Array, getReader(), then read()');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      const view = new Uint8Array(16);
      view[0] = 0x01;
      view[8] = 0x02;
      c.enqueue(view);
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const byobReader = stream.getReader({ mode: 'byob' });

  return byobReader.read(new Uint8Array(8)).then(result => {
    assert_false(result.done, 'done');

    const view = result.value;
    assert_equals(view.constructor, Uint8Array, 'value.constructor');
    assert_equals(view.buffer.byteLength, 8, 'value.buffer.byteLength');
    assert_equals(view.byteOffset, 0, 'value.byteOffset');
    assert_equals(view.byteLength, 8, 'value.byteLength');
    assert_equals(view[0], 0x01);

    byobReader.releaseLock();

    const reader = stream.getReader();

    return reader.read();
  }).then(result => {
    assert_false(result.done, 'done');

    const view = result.value;
    assert_equals(view.constructor, Uint8Array, 'value.constructor');
    assert_equals(view.buffer.byteLength, 16, 'value.buffer.byteLength');
    assert_equals(view.byteOffset, 8, 'value.byteOffset');
    assert_equals(view.byteLength, 8, 'value.byteLength');
    assert_equals(view[0], 0x02);
  });
}, 'ReadableStream with byte source: enqueue(), read(view) partially, then read()');

promise_test(t => {
  let controller;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader();

  controller.enqueue(new Uint8Array(16));
  controller.close();

  return reader.read().then(result => {
    assert_false(result.done, 'done');

    const view = result.value;
    assert_equals(view.byteOffset, 0, 'byteOffset');
    assert_equals(view.byteLength, 16, 'byteLength');

    return reader.read();
  }).then(result => {
    assert_true(result.done, 'done');
    assert_equals(result.value, undefined, 'value');
  });
}, 'ReadableStream with byte source: getReader(), enqueue(), close(), then read()');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      c.enqueue(new Uint8Array(16));
      c.close();
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader();

  return reader.read().then(result => {
    assert_false(result.done, 'done');

    const view = result.value;
    assert_equals(view.byteOffset, 0, 'byteOffset');
    assert_equals(view.byteLength, 16, 'byteLength');

    return reader.read();
  }).then(result => {
    assert_true(result.done, 'done');
    assert_equals(result.value, undefined, 'value');
  });
}, 'ReadableStream with byte source: enqueue(), close(), getReader(), then read()');

promise_test(() => {
  let controller;
  let byobRequest;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      controller.enqueue(new Uint8Array(16));
      byobRequest = controller.byobRequest;
    },
    type: 'bytes'
  });

  const reader = stream.getReader();

  return reader.read().then(result => {
    assert_false(result.done, 'done');
    assert_equals(result.value.byteLength, 16, 'byteLength');
    assert_equals(byobRequest, null, 'byobRequest must be null');
  });
}, 'ReadableStream with byte source: Respond to pull() by enqueue()');

promise_test(() => {
  let pullCount = 0;

  let controller;
  let byobRequest;
  const desiredSizes = [];

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      byobRequest = controller.byobRequest;
      desiredSizes.push(controller.desiredSize);
      controller.enqueue(new Uint8Array(1));
      desiredSizes.push(controller.desiredSize);
      controller.enqueue(new Uint8Array(1));
      desiredSizes.push(controller.desiredSize);

      ++pullCount;
    },
    type: 'bytes'
  }, {
    highWaterMark: 0
  });

  const reader = stream.getReader();

  const p0 = reader.read();
  const p1 = reader.read();
  const p2 = reader.read();

  // Respond to the first pull call.
  controller.enqueue(new Uint8Array(1));

  assert_equals(pullCount, 0, 'pullCount after the enqueue() outside pull');

  return Promise.all([p0, p1, p2]).then(result => {
    assert_equals(pullCount, 1, 'pullCount after completion of all read()s');

    assert_equals(result[0].done, false, 'result[0].done');
    assert_equals(result[0].value.byteLength, 1, 'result[0].value.byteLength');
    assert_equals(result[1].done, false, 'result[1].done');
    assert_equals(result[1].value.byteLength, 1, 'result[1].value.byteLength');
    assert_equals(result[2].done, false, 'result[2].done');
    assert_equals(result[2].value.byteLength, 1, 'result[2].value.byteLength');
    assert_equals(byobRequest, null, 'byobRequest should be null');
    assert_equals(desiredSizes[0], 0, 'desiredSize on pull should be 0');
    assert_equals(desiredSizes[1], 0, 'desiredSize after 1st enqueue() should be 0');
    assert_equals(desiredSizes[2], 0, 'desiredSize after 2nd enqueue() should be 0');
    assert_equals(pullCount, 1, 'pull() should only be called once');
  });
}, 'ReadableStream with byte source: Respond to pull() by enqueue() asynchronously');

promise_test(() => {
  let pullCount = 0;

  let byobRequest;
  const desiredSizes = [];

  const stream = new ReadableStream({
    pull(c) {
      byobRequest = c.byobRequest;
      desiredSizes.push(c.desiredSize);

      if (pullCount < 3) {
        c.enqueue(new Uint8Array(1));
      } else {
        c.close();
      }

      ++pullCount;
    },
    type: 'bytes'
  }, {
    highWaterMark: 256
  });

  const reader = stream.getReader();

  const p0 = reader.read();
  const p1 = reader.read();
  const p2 = reader.read();

  assert_equals(pullCount, 0, 'No pull as start() just finished and is not yet reflected to the state of the stream');

  return Promise.all([p0, p1, p2]).then(result => {
    assert_equals(pullCount, 4, 'pullCount after completion of all read()s');

    assert_equals(result[0].done, false, 'result[0].done');
    assert_equals(result[0].value.byteLength, 1, 'result[0].value.byteLength');
    assert_equals(result[1].done, false, 'result[1].done');
    assert_equals(result[1].value.byteLength, 1, 'result[1].value.byteLength');
    assert_equals(result[2].done, false, 'result[2].done');
    assert_equals(result[2].value.byteLength, 1, 'result[2].value.byteLength');
    assert_equals(byobRequest, null, 'byobRequest should be null');
    assert_equals(desiredSizes[0], 256, 'desiredSize on pull should be 256');
    assert_equals(desiredSizes[1], 256, 'desiredSize after 1st enqueue() should be 256');
    assert_equals(desiredSizes[2], 256, 'desiredSize after 2nd enqueue() should be 256');
    assert_equals(desiredSizes[3], 256, 'desiredSize after 3rd enqueue() should be 256');
  });
}, 'ReadableStream with byte source: Respond to multiple pull() by separate enqueue()');

promise_test(() => {
  let controller;

  let pullCount = 0;
  const byobRequestDefined = [];
  let byobRequestViewDefined;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      byobRequestDefined.push(controller.byobRequest !== null);
      const initialByobRequest = controller.byobRequest;

      const view = controller.byobRequest.view;
      view[0] = 0x01;
      controller.byobRequest.respond(1);

      byobRequestDefined.push(controller.byobRequest !== null);
      byobRequestViewDefined = initialByobRequest.view !== null;

      ++pullCount;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint8Array(1)).then(result => {
    assert_false(result.done, 'result.done');
    assert_equals(result.value.byteLength, 1, 'result.value.byteLength');
    assert_equals(result.value[0], 0x01, 'result.value[0]');
    assert_equals(pullCount, 1, 'pull() should be called only once');
    assert_true(byobRequestDefined[0], 'byobRequest must not be null before respond()');
    assert_false(byobRequestDefined[1], 'byobRequest must be null after respond()');
    assert_false(byobRequestViewDefined, 'view of initial byobRequest must be null after respond()');
  });
}, 'ReadableStream with byte source: read(view), then respond()');

promise_test(() => {
  let controller;

  let pullCount = 0;
  const byobRequestDefined = [];
  let byobRequestViewDefined;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      byobRequestDefined.push(controller.byobRequest !== null);
      const initialByobRequest = controller.byobRequest;

      const transferredView = transferArrayBufferView(controller.byobRequest.view);
      transferredView[0] = 0x01;
      controller.byobRequest.respondWithNewView(transferredView);

      byobRequestDefined.push(controller.byobRequest !== null);
      byobRequestViewDefined = initialByobRequest.view !== null;

      ++pullCount;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint8Array(1)).then(result => {
    assert_false(result.done, 'result.done');
    assert_equals(result.value.byteLength, 1, 'result.value.byteLength');
    assert_equals(result.value[0], 0x01, 'result.value[0]');
    assert_equals(pullCount, 1, 'pull() should be called only once');
    assert_true(byobRequestDefined[0], 'byobRequest must not be null before respondWithNewView()');
    assert_false(byobRequestDefined[1], 'byobRequest must be null after respondWithNewView()');
    assert_false(byobRequestViewDefined, 'view of initial byobRequest must be null after respondWithNewView()');
  });
}, 'ReadableStream with byte source: read(view), then respondWithNewView() with a transferred ArrayBuffer');

promise_test(() => {
  let controller;
  let byobRequestWasDefined;
  let incorrectRespondException;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      byobRequestWasDefined = controller.byobRequest !== null;

      try {
        controller.byobRequest.respond(2);
      } catch (e) {
        incorrectRespondException = e;
      }

      controller.byobRequest.respond(1);
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint8Array(1)).then(() => {
    assert_true(byobRequestWasDefined, 'byobRequest should be non-null');
    assert_not_equals(incorrectRespondException, undefined, 'respond() must throw');
    assert_equals(incorrectRespondException.name, 'RangeError', 'respond() must throw a RangeError');
  });
}, 'ReadableStream with byte source: read(view), then respond() with too big value');

promise_test(() => {
  let pullCount = 0;

  let controller;
  let byobRequest;
  let viewInfo;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      ++pullCount;

      byobRequest = controller.byobRequest;
      const view = byobRequest.view;
      viewInfo = extractViewInfo(view);

      view[0] = 0x01;
      view[1] = 0x02;
      view[2] = 0x03;

      controller.byobRequest.respond(3);
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint16Array(2)).then(result => {
    assert_equals(pullCount, 1);

    assert_false(result.done, 'done');

    const view = result.value;
    assert_equals(view.byteOffset, 0, 'byteOffset');
    assert_equals(view.byteLength, 2, 'byteLength');

    const dataView = new DataView(view.buffer, view.byteOffset, view.byteLength);
    assert_equals(dataView.getUint16(0), 0x0102);

    return reader.read(new Uint8Array(1));
  }).then(result => {
    assert_equals(pullCount, 1);
    assert_not_equals(byobRequest, null, 'byobRequest must not be null');
    assert_equals(viewInfo.constructor, Uint8Array, 'view.constructor should be Uint8Array');
    assert_equals(viewInfo.bufferByteLength, 4, 'view.buffer.byteLength should be 4');
    assert_equals(viewInfo.byteOffset, 0, 'view.byteOffset should be 0');
    assert_equals(viewInfo.byteLength, 4, 'view.byteLength should be 4');

    assert_false(result.done, 'done');

    const view = result.value;
    assert_equals(view.byteOffset, 0, 'byteOffset');
    assert_equals(view.byteLength, 1, 'byteLength');

    assert_equals(view[0], 0x03);
  });
}, 'ReadableStream with byte source: respond(3) to read(view) with 2 element Uint16Array enqueues the 1 byte ' +
   'remainder');

promise_test(t => {
  const stream = new ReadableStream({
    start(controller) {
      const view = new Uint8Array(16);
      view[15] = 0x01;
      controller.enqueue(view);
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint8Array(16)).then(result => {
    assert_false(result.done);

    const view = result.value;
    assert_equals(view.byteOffset, 0);
    assert_equals(view.byteLength, 16);
    assert_equals(view[15], 0x01);
  });
}, 'ReadableStream with byte source: enqueue(), getReader(), then read(view)');

promise_test(t => {
  let cancelCount = 0;
  let reason;

  const passedReason = new TypeError('foo');

  const stream = new ReadableStream({
    start(c) {
      c.enqueue(new Uint8Array(16));
    },
    pull: t.unreached_func('pull() should not be called'),
    cancel(r) {
      if (cancelCount === 0) {
        reason = r;
      }

      ++cancelCount;
    },
    type: 'bytes'
  });

  const reader = stream.getReader();

  return reader.cancel(passedReason).then(result => {
    assert_equals(result, undefined);
    assert_equals(cancelCount, 1);
    assert_equals(reason, passedReason, 'reason should equal the passed reason');
  });
}, 'ReadableStream with byte source: enqueue(), getReader(), then cancel() (mode = not BYOB)');

promise_test(t => {
  let cancelCount = 0;
  let reason;

  const passedReason = new TypeError('foo');

  const stream = new ReadableStream({
    start(c) {
      c.enqueue(new Uint8Array(16));
    },
    pull: t.unreached_func('pull() should not be called'),
    cancel(r) {
      if (cancelCount === 0) {
        reason = r;
      }

      ++cancelCount;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.cancel(passedReason).then(result => {
    assert_equals(result, undefined);
    assert_equals(cancelCount, 1);
    assert_equals(reason, passedReason, 'reason should equal the passed reason');
  });
}, 'ReadableStream with byte source: enqueue(), getReader(), then cancel() (mode = BYOB)');

promise_test(t => {
  let cancelCount = 0;
  let reason;

  const passedReason = new TypeError('foo');

  let controller;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull: t.unreached_func('pull() should not be called'),
    cancel(r) {
      if (cancelCount === 0) {
        reason = r;
      }

      ++cancelCount;

      return 'bar';
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  const readPromise = reader.read(new Uint8Array(1)).then(result => {
    assert_true(result.done, 'result.done');
    assert_equals(result.value, undefined, 'result.value');
  });

  const cancelPromise = reader.cancel(passedReason).then(result => {
    assert_equals(result, undefined, 'cancel() return value should be fulfilled with undefined');
    assert_equals(cancelCount, 1, 'cancel() should be called only once');
    assert_equals(reason, passedReason, 'reason should equal the passed reason');
  });

  return Promise.all([readPromise, cancelPromise]);
}, 'ReadableStream with byte source: getReader(), read(view), then cancel()');

promise_test(() => {
  let pullCount = 0;

  let controller;
  let byobRequest;
  const viewInfos = [];

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      byobRequest = controller.byobRequest;

      viewInfos.push(extractViewInfo(controller.byobRequest.view));
      controller.enqueue(new Uint8Array(1));
      viewInfos.push(extractViewInfo(controller.byobRequest.view));

      ++pullCount;
    },
    type: 'bytes'
  });

  return Promise.resolve().then(() => {
    assert_equals(pullCount, 0, 'No pull() as no read(view) yet');

    const reader = stream.getReader({ mode: 'byob' });

    const promise = reader.read(new Uint16Array(1)).then(result => {
      assert_true(result.done, 'result.done');
      assert_equals(result.value, undefined, 'result.value');
    });

    assert_equals(pullCount, 1, '1 pull() should have been made in response to partial fill by enqueue()');
    assert_not_equals(byobRequest, null, 'byobRequest should not be null');
    assert_equals(viewInfos[0].byteLength, 2, 'byteLength before enqueue() should be 2');
    assert_equals(viewInfos[1].byteLength, 1, 'byteLength after enqueue() should be 1');

    reader.cancel();

    assert_equals(pullCount, 1, 'pull() should only be called once');
    return promise;
  });
}, 'ReadableStream with byte source: cancel() with partially filled pending pull() request');

promise_test(() => {
  let controller;
  let pullCalled = false;

  const stream = new ReadableStream({
    start(c) {
      const view = new Uint8Array(8);
      view[7] = 0x01;
      c.enqueue(view);

      controller = c;
    },
    pull() {
      pullCalled = true;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  const buffer = new ArrayBuffer(16);

  return reader.read(new Uint8Array(buffer, 8, 8)).then(result => {
    assert_false(result.done);

    assert_false(pullCalled, 'pull() must not have been called');

    const view = result.value;
    assert_equals(view.constructor, Uint8Array);
    assert_equals(view.buffer.byteLength, 16);
    assert_equals(view.byteOffset, 8);
    assert_equals(view.byteLength, 8);
    assert_equals(view[7], 0x01);
  });
}, 'ReadableStream with byte source: enqueue(), getReader(), then read(view) where view.buffer is not fully ' +
   'covered by view');

promise_test(() => {
  let controller;
  let pullCalled = false;

  const stream = new ReadableStream({
    start(c) {
      let view;

      view = new Uint8Array(16);
      view[15] = 123;
      c.enqueue(view);

      view = new Uint8Array(8);
      view[7] = 111;
      c.enqueue(view);

      controller = c;
    },
    pull() {
      pullCalled = true;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint8Array(24)).then(result => {
    assert_false(result.done, 'done');

    assert_false(pullCalled, 'pull() must not have been called');

    const view = result.value;
    assert_equals(view.byteOffset, 0, 'byteOffset');
    assert_equals(view.byteLength, 24, 'byteLength');
    assert_equals(view[15], 123, 'Contents are set from the first chunk');
    assert_equals(view[23], 111, 'Contents are set from the second chunk');
  });
}, 'ReadableStream with byte source: Multiple enqueue(), getReader(), then read(view)');

promise_test(() => {
  let pullCalled = false;

  const stream = new ReadableStream({
    start(c) {
      const view = new Uint8Array(16);
      view[15] = 0x01;
      c.enqueue(view);
    },
    pull() {
      pullCalled = true;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint8Array(24)).then(result => {
    assert_false(result.done);

    assert_false(pullCalled, 'pull() must not have been called');

    const view = result.value;
    assert_equals(view.byteOffset, 0);
    assert_equals(view.byteLength, 16);
    assert_equals(view[15], 0x01);
  });
}, 'ReadableStream with byte source: enqueue(), getReader(), then read(view) with a bigger view');

promise_test(() => {
  let pullCalled = false;

  const stream = new ReadableStream({
    start(c) {
      const view = new Uint8Array(16);
      view[7] = 0x01;
      view[15] = 0x02;
      c.enqueue(view);
    },
    pull() {
      pullCalled = true;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint8Array(8)).then(result => {
    assert_false(result.done, 'done');

    const view = result.value;
    assert_equals(view.byteOffset, 0);
    assert_equals(view.byteLength, 8);
    assert_equals(view[7], 0x01);

    return reader.read(new Uint8Array(8));
  }).then(result => {
    assert_false(result.done, 'done');

    assert_false(pullCalled, 'pull() must not have been called');

    const view = result.value;
    assert_equals(view.byteOffset, 0);
    assert_equals(view.byteLength, 8);
    assert_equals(view[7], 0x02);
  });
}, 'ReadableStream with byte source: enqueue(), getReader(), then read(view) with smaller views');

promise_test(() => {
  let controller;
  let viewInfo;

  const stream = new ReadableStream({
    start(c) {
      const view = new Uint8Array(1);
      view[0] = 0xff;
      c.enqueue(view);

      controller = c;
    },
    pull() {
      if (controller.byobRequest === null) {
        return;
      }

      const view = controller.byobRequest.view;
      viewInfo = extractViewInfo(view);

      view[0] = 0xaa;
      controller.byobRequest.respond(1);
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint16Array(1)).then(result => {
    assert_false(result.done);

    const view = result.value;
    assert_equals(view.byteOffset, 0);
    assert_equals(view.byteLength, 2);

    const dataView = new DataView(view.buffer, view.byteOffset, view.byteLength);
    assert_equals(dataView.getUint16(0), 0xffaa);

    assert_equals(viewInfo.constructor, Uint8Array, 'view.constructor should be Uint8Array');
    assert_equals(viewInfo.bufferByteLength, 2, 'view.buffer.byteLength should be 2');
    assert_equals(viewInfo.byteOffset, 1, 'view.byteOffset should be 1');
    assert_equals(viewInfo.byteLength, 1, 'view.byteLength should be 1');
  });
}, 'ReadableStream with byte source: enqueue() 1 byte, getReader(), then read(view) with Uint16Array');

promise_test(() => {
  let pullCount = 0;

  let controller;
  let byobRequest;
  let viewInfo;
  let desiredSize;

  const stream = new ReadableStream({
    start(c) {
      const view = new Uint8Array(3);
      view[0] = 0x01;
      view[2] = 0x02;
      c.enqueue(view);

      controller = c;
    },
    pull() {
      byobRequest = controller.byobRequest;

      const view = controller.byobRequest.view;

      viewInfo = extractViewInfo(view);

      view[0] = 0x03;
      controller.byobRequest.respond(1);

      desiredSize = controller.desiredSize;

      ++pullCount;
    },
    type: 'bytes'
  });

  // Wait for completion of the start method to be reflected.
  return Promise.resolve().then(() => {
    const reader = stream.getReader({ mode: 'byob' });

    const promise = reader.read(new Uint16Array(2)).then(result => {
      assert_false(result.done, 'done');

      const view = result.value;
      assert_equals(view.constructor, Uint16Array, 'constructor');
      assert_equals(view.buffer.byteLength, 4, 'buffer.byteLength');
      assert_equals(view.byteOffset, 0, 'byteOffset');
      assert_equals(view.byteLength, 2, 'byteLength');

      const dataView = new DataView(view.buffer, view.byteOffset, view.byteLength);
      assert_equals(dataView.getUint16(0), 0x0100, 'contents are set');

      const p = reader.read(new Uint16Array(1));

      assert_equals(pullCount, 1);

      return p;
    }).then(result => {
      assert_false(result.done, 'done');

      const view = result.value;
      assert_equals(view.buffer.byteLength, 2, 'buffer.byteLength');
      assert_equals(view.byteOffset, 0, 'byteOffset');
      assert_equals(view.byteLength, 2, 'byteLength');

      const dataView = new DataView(view.buffer, view.byteOffset, view.byteLength);
      assert_equals(dataView.getUint16(0), 0x0203, 'contents are set');

      assert_not_equals(byobRequest, null, 'byobRequest must not be null');
      assert_equals(viewInfo.constructor, Uint8Array, 'view.constructor should be Uint8Array');
      assert_equals(viewInfo.bufferByteLength, 2, 'view.buffer.byteLength should be 2');
      assert_equals(viewInfo.byteOffset, 1, 'view.byteOffset should be 1');
      assert_equals(viewInfo.byteLength, 1, 'view.byteLength should be 1');
      assert_equals(desiredSize, 0, 'desiredSize should be zero');
    });

    assert_equals(pullCount, 0);

    return promise;
  });
}, 'ReadableStream with byte source: enqueue() 3 byte, getReader(), then read(view) with 2-element Uint16Array');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      const view = new Uint8Array(1);
      view[0] = 0xff;
      c.enqueue(view);
      c.close();
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });


  return promise_rejects_js(t, TypeError, reader.read(new Uint16Array(1)), 'read(view) must fail')
      .then(() => promise_rejects_js(t, TypeError, reader.closed, 'reader.closed should reject'));
}, 'ReadableStream with byte source: read(view) with Uint16Array on close()-d stream with 1 byte enqueue()-d must ' +
   'fail');

promise_test(t => {
  let controller;

  const stream = new ReadableStream({
    start(c) {
      const view = new Uint8Array(1);
      view[0] = 0xff;
      c.enqueue(view);

      controller = c;
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  const readPromise = reader.read(new Uint16Array(1));

  assert_throws_js(TypeError, () => controller.close(), 'controller.close() must throw');

  return promise_rejects_js(t, TypeError, readPromise, 'read(view) must fail')
      .then(() => promise_rejects_js(t, TypeError, reader.closed, 'reader.closed must reject'));
}, 'ReadableStream with byte source: A stream must be errored if close()-d before fulfilling read(view) with ' +
   'Uint16Array');

test(() => {
  let controller;

  new ReadableStream({
    start(c) {
      controller = c;
    },
    type: 'bytes'
  });

  // Enqueue a chunk so that the stream doesn't get closed. This is to check duplicate close() calls are rejected
  // even if the stream has not yet entered the closed state.
  const view = new Uint8Array(1);
  controller.enqueue(view);
  controller.close();

  assert_throws_js(TypeError, () => controller.close(), 'controller.close() must throw');
}, 'ReadableStream with byte source: Throw if close()-ed more than once');

test(() => {
  let controller;

  new ReadableStream({
    start(c) {
      controller = c;
    },
    type: 'bytes'
  });

  // Enqueue a chunk so that the stream doesn't get closed. This is to check enqueue() after close() is  rejected
  // even if the stream has not yet entered the closed state.
  const view = new Uint8Array(1);
  controller.enqueue(view);
  controller.close();

  assert_throws_js(TypeError, () => controller.enqueue(view), 'controller.close() must throw');
}, 'ReadableStream with byte source: Throw on enqueue() after close()');

promise_test(() => {
  let controller;
  let byobRequest;
  let viewInfo;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      byobRequest = controller.byobRequest;
      const view = controller.byobRequest.view;
      viewInfo = extractViewInfo(view);

      view[15] = 0x01;
      controller.byobRequest.respond(16);
      controller.close();
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint8Array(16)).then(result => {
    assert_false(result.done);

    const view = result.value;
    assert_equals(view.byteOffset, 0);
    assert_equals(view.byteLength, 16);
    assert_equals(view[15], 0x01);

    return reader.read(new Uint8Array(16));
  }).then(result => {
    assert_true(result.done);

    const view = result.value;
    assert_equals(view.byteOffset, 0);
    assert_equals(view.byteLength, 0);

    assert_not_equals(byobRequest, null, 'byobRequest must not be null');
    assert_equals(viewInfo.constructor, Uint8Array, 'view.constructor should be Uint8Array');
    assert_equals(viewInfo.bufferByteLength, 16, 'view.buffer.byteLength should be 16');
    assert_equals(viewInfo.byteOffset, 0, 'view.byteOffset should be 0');
    assert_equals(viewInfo.byteLength, 16, 'view.byteLength should be 16');
  });
}, 'ReadableStream with byte source: read(view), then respond() and close() in pull()');

promise_test(() => {
  let pullCount = 0;

  let controller;
  const viewInfos = [];
  const viewInfosAfterRespond = [];

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      if (controller.byobRequest === null) {
        return;
      }

      for (let i = 0; i < 4; ++i) {
        const view = controller.byobRequest.view;
        viewInfos.push(extractViewInfo(view));

        view[0] = 0x01;
        controller.byobRequest.respond(1);
        viewInfosAfterRespond.push(extractViewInfo(view));
      }

      ++pullCount;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint32Array(1)).then(result => {
    assert_false(result.done, 'result.done');

    const view = result.value;
    assert_equals(view.byteOffset, 0, 'result.value.byteOffset');
    assert_equals(view.byteLength, 4, 'result.value.byteLength');
    assert_equals(view[0], 0x01010101, 'result.value[0]');

    assert_equals(pullCount, 1, 'pull() should only be called once');

    for (let i = 0; i < 4; ++i) {
      assert_equals(viewInfos[i].constructor, Uint8Array, 'view.constructor should be Uint8Array');
      assert_equals(viewInfos[i].bufferByteLength, 4, 'view.buffer.byteLength should be 4');

      assert_equals(viewInfos[i].byteOffset, i, 'view.byteOffset should be i');
      assert_equals(viewInfos[i].byteLength, 4 - i, 'view.byteLength should be 4 - i');

      assert_equals(viewInfosAfterRespond[i].bufferByteLength, 0, 'view.buffer should be transferred after respond()');
    }
  });
}, 'ReadableStream with byte source: read(view) with Uint32Array, then fill it by multiple respond() calls');

promise_test(() => {
  let pullCount = 0;

  let controller;
  const viewInfos = [];
  const viewInfosAfterEnqueue = [];

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      if (controller.byobRequest === null) {
        return;
      }

      for (let i = 0; i < 4; ++i) {
        const view = controller.byobRequest.view;
        viewInfos.push(extractViewInfo(view));

        controller.enqueue(new Uint8Array([0x01]));
        viewInfosAfterEnqueue.push(extractViewInfo(view));
      }

      ++pullCount;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return reader.read(new Uint32Array(1)).then(result => {
    assert_false(result.done, 'result.done');

    const view = result.value;
    assert_equals(view.byteOffset, 0, 'result.value.byteOffset');
    assert_equals(view.byteLength, 4, 'result.value.byteLength');
    assert_equals(view[0], 0x01010101, 'result.value[0]');

    assert_equals(pullCount, 1, 'pull() should only be called once');

    for (let i = 0; i < 4; ++i) {
      assert_equals(viewInfos[i].constructor, Uint8Array, 'view.constructor should be Uint8Array');
      assert_equals(viewInfos[i].bufferByteLength, 4, 'view.buffer.byteLength should be 4');

      assert_equals(viewInfos[i].byteOffset, i, 'view.byteOffset should be i');
      assert_equals(viewInfos[i].byteLength, 4 - i, 'view.byteLength should be 4 - i');

      assert_equals(viewInfosAfterEnqueue[i].bufferByteLength, 0, 'view.buffer should be transferred after enqueue()');
    }
  });
}, 'ReadableStream with byte source: read(view) with Uint32Array, then fill it by multiple enqueue() calls');

promise_test(() => {
  let pullCount = 0;

  let controller;
  let byobRequest;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      byobRequest = controller.byobRequest;

      ++pullCount;
    },
    type: 'bytes'
  });

  const reader = stream.getReader();

  const p0 = reader.read().then(result => {
    assert_equals(pullCount, 1);

    controller.enqueue(new Uint8Array(2));

    // Since the queue has data no less than HWM, no more pull.
    assert_equals(pullCount, 1);

    assert_false(result.done);

    const view = result.value;
    assert_equals(view.constructor, Uint8Array);
    assert_equals(view.buffer.byteLength, 1);
    assert_equals(view.byteOffset, 0);
    assert_equals(view.byteLength, 1);
  });

  assert_equals(pullCount, 0, 'No pull should have been made since the startPromise has not yet been handled');

  const p1 = reader.read().then(result => {
    assert_equals(pullCount, 1);

    assert_false(result.done);

    const view = result.value;
    assert_equals(view.constructor, Uint8Array);
    assert_equals(view.buffer.byteLength, 2);
    assert_equals(view.byteOffset, 0);
    assert_equals(view.byteLength, 2);

    assert_equals(byobRequest, null, 'byobRequest must be null');
  });

  assert_equals(pullCount, 0, 'No pull should have been made since the startPromise has not yet been handled');

  controller.enqueue(new Uint8Array(1));

  assert_equals(pullCount, 0, 'No pull should have been made since the startPromise has not yet been handled');

  return Promise.all([p0, p1]);
}, 'ReadableStream with byte source: read() twice, then enqueue() twice');

promise_test(t => {
  let controller;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  const p0 = reader.read(new Uint8Array(16)).then(result => {
    assert_true(result.done, '1st read: done');

    const view = result.value;
    assert_equals(view.buffer.byteLength, 16, '1st read: buffer.byteLength');
    assert_equals(view.byteOffset, 0, '1st read: byteOffset');
    assert_equals(view.byteLength, 0, '1st read: byteLength');
  });

  const p1 = reader.read(new Uint8Array(32)).then(result => {
    assert_true(result.done, '2nd read: done');

    const view = result.value;
    assert_equals(view.buffer.byteLength, 32, '2nd read: buffer.byteLength');
    assert_equals(view.byteOffset, 0, '2nd read: byteOffset');
    assert_equals(view.byteLength, 0, '2nd read: byteLength');
  });

  controller.close();
  controller.byobRequest.respond(0);

  return Promise.all([p0, p1]);
}, 'ReadableStream with byte source: Multiple read(view), close() and respond()');

promise_test(t => {
  let controller;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  const p0 = reader.read(new Uint8Array(16)).then(result => {
    assert_false(result.done, '1st read: done');

    const view = result.value;
    assert_equals(view.buffer.byteLength, 16, '1st read: buffer.byteLength');
    assert_equals(view.byteOffset, 0, '1st read: byteOffset');
    assert_equals(view.byteLength, 16, '1st read: byteLength');
  });

  const p1 = reader.read(new Uint8Array(16)).then(result => {
    assert_false(result.done, '2nd read: done');

    const view = result.value;
    assert_equals(view.buffer.byteLength, 16, '2nd read: buffer.byteLength');
    assert_equals(view.byteOffset, 0, '2nd read: byteOffset');
    assert_equals(view.byteLength, 8, '2nd read: byteLength');
  });

  controller.enqueue(new Uint8Array(24));

  return Promise.all([p0, p1]);
}, 'ReadableStream with byte source: Multiple read(view), big enqueue()');

promise_test(t => {
  let controller;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  let bytesRead = 0;

  function pump() {
    return reader.read(new Uint8Array(7)).then(result => {
      if (result.done) {
        assert_equals(bytesRead, 1024);
        return undefined;
      }

      bytesRead += result.value.byteLength;

      return pump();
    });
  }
  const promise = pump();

  controller.enqueue(new Uint8Array(512));
  controller.enqueue(new Uint8Array(512));
  controller.close();

  return promise;
}, 'ReadableStream with byte source: Multiple read(view) and multiple enqueue()');

promise_test(t => {
  let pullCalled = false;
  const stream = new ReadableStream({
    pull(controller) {
      pullCalled = true;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return promise_rejects_js(t, TypeError, reader.read(), 'read() must fail')
      .then(() => assert_false(pullCalled, 'pull() must not have been called'));
}, 'ReadableStream with byte source: read(view) with passing undefined as view must fail');

promise_test(t => {
  const stream = new ReadableStream({
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return promise_rejects_js(t, TypeError, reader.read({}), 'read(view) must fail');
}, 'ReadableStream with byte source: read(view) with passing an empty object as view must fail');

promise_test(t => {
  const stream = new ReadableStream({
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return promise_rejects_js(t, TypeError,
                         reader.read({ buffer: new ArrayBuffer(10), byteOffset: 0, byteLength: 10 }),
                         'read(view) must fail');
}, 'ReadableStream with byte source: Even read(view) with passing ArrayBufferView like object as view must fail');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      c.error(error1);
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader();

  return promise_rejects_exactly(t, error1, reader.read(), 'read() must fail');
}, 'ReadableStream with byte source: read() on an errored stream');

promise_test(t => {
  let controller;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    type: 'bytes'
  });

  const reader = stream.getReader();

  const promise = promise_rejects_exactly(t, error1, reader.read(), 'read() must fail');

  controller.error(error1);

  return promise;
}, 'ReadableStream with byte source: read(), then error()');

promise_test(t => {
  const stream = new ReadableStream({
    start(c) {
      c.error(error1);
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return promise_rejects_exactly(t, error1, reader.read(new Uint8Array(1)), 'read() must fail');
}, 'ReadableStream with byte source: read(view) on an errored stream');

promise_test(t => {
  let controller;

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  const promise = promise_rejects_exactly(t, error1, reader.read(new Uint8Array(1)), 'read() must fail');

  controller.error(error1);

  return promise;
}, 'ReadableStream with byte source: read(view), then error()');

promise_test(t => {
  let controller;
  let byobRequest;

  const testError = new TypeError('foo');

  const stream = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      byobRequest = controller.byobRequest;
      throw testError;
    },
    type: 'bytes'
  });

  const reader = stream.getReader();

  const promise = promise_rejects_exactly(t, testError, reader.read(), 'read() must fail');
  return promise_rejects_exactly(t, testError, promise.then(() => reader.closed))
      .then(() => assert_equals(byobRequest, null, 'byobRequest must be null'));
}, 'ReadableStream with byte source: Throwing in pull function must error the stream');

promise_test(t => {
  let byobRequest;

  const stream = new ReadableStream({
    pull(controller) {
      byobRequest = controller.byobRequest;
      controller.error(error1);
      throw new TypeError('foo');
    },
    type: 'bytes'
  });

  const reader = stream.getReader();

  return promise_rejects_exactly(t, error1, reader.read(), 'read() must fail')
      .then(() => promise_rejects_exactly(t, error1, reader.closed, 'closed must fail'))
      .then(() => assert_equals(byobRequest, null, 'byobRequest must be null'));
}, 'ReadableStream with byte source: Throwing in pull in response to read() must be ignored if the stream is ' +
   'errored in it');

promise_test(t => {
  let byobRequest;

  const testError = new TypeError('foo');

  const stream = new ReadableStream({
    pull(controller) {
      byobRequest = controller.byobRequest;
      throw testError;
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return promise_rejects_exactly(t, testError, reader.read(new Uint8Array(1)), 'read(view) must fail')
      .then(() => promise_rejects_exactly(t, testError, reader.closed, 'reader.closed must reject'))
      .then(() => assert_not_equals(byobRequest, null, 'byobRequest must not be null'));
}, 'ReadableStream with byte source: Throwing in pull in response to read(view) function must error the stream');

promise_test(t => {
  let byobRequest;

  const stream = new ReadableStream({
    pull(controller) {
      byobRequest = controller.byobRequest;
      controller.error(error1);
      throw new TypeError('foo');
    },
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });

  return promise_rejects_exactly(t, error1, reader.read(new Uint8Array(1)), 'read(view) must fail')
      .then(() => promise_rejects_exactly(t, error1, reader.closed, 'closed must fail'))
      .then(() => assert_not_equals(byobRequest, null, 'byobRequest must not be null'));
}, 'ReadableStream with byte source: Throwing in pull in response to read(view) must be ignored if the stream is ' +
   'errored in it');

promise_test(() => {
  let byobRequest;
  const rs = new ReadableStream({
    pull(controller) {
      byobRequest = controller.byobRequest;
      byobRequest.respond(4);
    },
    type: 'bytes'
  });
  const reader = rs.getReader({ mode: 'byob' });
  const view = new Uint8Array(16);
  return reader.read(view).then(() => {
    assert_throws_js(TypeError, () => byobRequest.respond(4), 'respond() should throw a TypeError');
  });
}, 'calling respond() twice on the same byobRequest should throw');

promise_test(() => {
  let byobRequest;
  const newView = () => new Uint8Array(16);
  const rs = new ReadableStream({
    pull(controller) {
      byobRequest = controller.byobRequest;
      byobRequest.respondWithNewView(newView());
    },
    type: 'bytes'
  });
  const reader = rs.getReader({ mode: 'byob' });
  return reader.read(newView()).then(() => {
    assert_throws_js(TypeError, () => byobRequest.respondWithNewView(newView()),
                     'respondWithNewView() should throw a TypeError');
  });
}, 'calling respondWithNewView() twice on the same byobRequest should throw');

promise_test(() => {
  let controller;
  let byobRequest;
  let resolvePullCalledPromise;
  const pullCalledPromise = new Promise(resolve => {
    resolvePullCalledPromise = resolve;
  });
  let resolvePull;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull(c) {
      byobRequest = c.byobRequest;
      resolvePullCalledPromise();
      return new Promise(resolve => {
        resolvePull = resolve;
      });
    },
    type: 'bytes'
  });
  const reader = rs.getReader({ mode: 'byob' });
  const readPromise = reader.read(new Uint8Array(16));
  return pullCalledPromise.then(() => {
    controller.close();
    byobRequest.respond(0);
    resolvePull();
    return readPromise.then(() => {
      assert_throws_js(TypeError, () => byobRequest.respond(0), 'respond() should throw');
    });
  });
}, 'calling respond(0) twice on the same byobRequest should throw even when closed');

promise_test(() => {
  let controller;
  let byobRequest;
  let resolvePullCalledPromise;
  const pullCalledPromise = new Promise(resolve => {
    resolvePullCalledPromise = resolve;
  });
  let resolvePull;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull(c) {
      byobRequest = c.byobRequest;
      resolvePullCalledPromise();
      return new Promise(resolve => {
        resolvePull = resolve;
      });
    },
    type: 'bytes'
  });
  const reader = rs.getReader({ mode: 'byob' });
  const readPromise = reader.read(new Uint8Array(16));
  return pullCalledPromise.then(() => {
    const cancelPromise = reader.cancel('meh');
    assert_throws_js(TypeError, () => byobRequest.respond(0), 'respond() should throw');
    resolvePull();
    return Promise.all([readPromise, cancelPromise]);
  });
}, 'calling respond() should throw when canceled');

promise_test(async t => {
  let resolvePullCalledPromise;
  const pullCalledPromise = new Promise(resolve => {
    resolvePullCalledPromise = resolve;
  });
  let resolvePull;
  const rs = new ReadableStream({
    pull() {
      resolvePullCalledPromise();
      return new Promise(resolve => {
        resolvePull = resolve;
      });
    },
    type: 'bytes'
  });
  const reader = rs.getReader({ mode: 'byob' });
  const read = reader.read(new Uint8Array(16));
  await pullCalledPromise;
  resolvePull();
  await delay(0);
  reader.releaseLock();
  await promise_rejects_js(t, TypeError, read, 'pending read should reject');
}, 'pull() resolving should not resolve read()');

promise_test(() => {
  // Tests https://github.com/whatwg/streams/issues/686

  let controller;
  const rs = new ReadableStream({
    autoAllocateChunkSize: 128,
    start(c) {
      controller = c;
    },
    type: 'bytes'
  });

  const readPromise = rs.getReader().read();

  const br = controller.byobRequest;
  controller.close();

  br.respond(0);

  return readPromise;
}, 'ReadableStream with byte source: default reader + autoAllocateChunkSize + byobRequest interaction');

test(() => {
  assert_throws_js(TypeError, () => new ReadableStream({ autoAllocateChunkSize: 0, type: 'bytes' }),
      'controller cannot be setup with autoAllocateChunkSize = 0');
}, 'ReadableStream with byte source: autoAllocateChunkSize cannot be 0');

test(() => {
  const ReadableStreamBYOBReader = new ReadableStream({ type: 'bytes' }).getReader({ mode: 'byob' }).constructor;
  const stream = new ReadableStream({ type: 'bytes' });
  new ReadableStreamBYOBReader(stream);
}, 'ReadableStreamBYOBReader can be constructed directly');

test(() => {
  const ReadableStreamBYOBReader = new ReadableStream({ type: 'bytes' }).getReader({ mode: 'byob' }).constructor;
  assert_throws_js(TypeError, () => new ReadableStreamBYOBReader({}), 'constructor must throw');
}, 'ReadableStreamBYOBReader constructor requires a ReadableStream argument');

test(() => {
  const ReadableStreamBYOBReader = new ReadableStream({ type: 'bytes' }).getReader({ mode: 'byob' }).constructor;
  const stream = new ReadableStream({ type: 'bytes' });
  stream.getReader();
  assert_throws_js(TypeError, () => new ReadableStreamBYOBReader(stream), 'constructor must throw');
}, 'ReadableStreamBYOBReader constructor requires an unlocked ReadableStream');

test(() => {
  const ReadableStreamBYOBReader = new ReadableStream({ type: 'bytes' }).getReader({ mode: 'byob' }).constructor;
  const stream = new ReadableStream();
  assert_throws_js(TypeError, () => new ReadableStreamBYOBReader(stream), 'constructor must throw');
}, 'ReadableStreamBYOBReader constructor requires a ReadableStream with type "bytes"');

test(() => {
  assert_throws_js(RangeError, () => new ReadableStream({ type: 'bytes' }, {
    size() {
      return 1;
    }
  }), 'constructor should throw for size function');

  assert_throws_js(RangeError,
                   () => new ReadableStream({ type: 'bytes' }, new CountQueuingStrategy({ highWaterMark: 1 })),
                   'constructor should throw when strategy is CountQueuingStrategy');

  assert_throws_js(RangeError,
                   () => new ReadableStream({ type: 'bytes' }, new ByteLengthQueuingStrategy({ highWaterMark: 512 })),
                   'constructor should throw when strategy is ByteLengthQueuingStrategy');

  class HasSizeMethod {
    size() {}
 }

  assert_throws_js(RangeError, () => new ReadableStream({ type: 'bytes' }, new HasSizeMethod()),
                   'constructor should throw when size on the prototype chain');
}, 'ReadableStream constructor should not accept a strategy with a size defined if type is "bytes"');

promise_test(async t => {
  const stream = new ReadableStream({
    pull: t.step_func(c => {
      const view = new Uint8Array(c.byobRequest.view.buffer, 0, 1);
      view[0] = 1;

      c.byobRequest.respondWithNewView(view);
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  const result = await reader.read(new Uint8Array([4, 5, 6]));
  assert_false(result.done, 'result.done');

  const view = result.value;
  assert_equals(view.byteOffset, 0, 'result.value.byteOffset');
  assert_equals(view.byteLength, 1, 'result.value.byteLength');
  assert_equals(view[0], 1, 'result.value[0]');
  assert_equals(view.buffer.byteLength, 3, 'result.value.buffer.byteLength');
  assert_array_equals([...new Uint8Array(view.buffer)], [1, 5, 6], 'result.value.buffer');
}, 'ReadableStream with byte source: respondWithNewView() with a smaller view');

promise_test(async t => {
  const stream = new ReadableStream({
    pull: t.step_func(c => {
      const view = new Uint8Array(c.byobRequest.view.buffer, 0, 0);

      c.close();

      c.byobRequest.respondWithNewView(view);
    }),
    type: 'bytes'
  });
  const reader = stream.getReader({ mode: 'byob' });

  const result = await reader.read(new Uint8Array([4, 5, 6]));
  assert_true(result.done, 'result.done');

  const view = result.value;
  assert_equals(view.byteOffset, 0, 'result.value.byteOffset');
  assert_equals(view.byteLength, 0, 'result.value.byteLength');
  assert_equals(view.buffer.byteLength, 3, 'result.value.buffer.byteLength');
  assert_array_equals([...new Uint8Array(view.buffer)], [4, 5, 6], 'result.value.buffer');
}, 'ReadableStream with byte source: respondWithNewView() with a zero-length view (in the closed state)');

promise_test(async t => {
  let controller;
  let resolvePullCalledPromise;
  const pullCalledPromise = new Promise(resolve => {
    resolvePullCalledPromise = resolve;
  });
  const stream = new ReadableStream({
    start: t.step_func((c) => {
      controller = c;
    }),
    pull: t.step_func(() => {
      resolvePullCalledPromise();
    }),
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });
  const readPromise = reader.read(new Uint8Array([4, 5, 6]));
  await pullCalledPromise;

  // Transfer the original BYOB request's buffer, and respond with a new view on that buffer
  const transferredView = transferArrayBufferView(controller.byobRequest.view);
  const newView = transferredView.subarray(0, 1);
  newView[0] = 42;

  controller.byobRequest.respondWithNewView(newView);

  const result = await readPromise;
  assert_false(result.done, 'result.done');

  const view = result.value;
  assert_equals(view.byteOffset, 0, 'result.value.byteOffset');
  assert_equals(view.byteLength, 1, 'result.value.byteLength');
  assert_equals(view[0], 42, 'result.value[0]');
  assert_equals(view.buffer.byteLength, 3, 'result.value.buffer.byteLength');
  assert_array_equals([...new Uint8Array(view.buffer)], [42, 5, 6], 'result.value.buffer');

}, 'ReadableStream with byte source: respondWithNewView() with a transferred non-zero-length view ' +
   '(in the readable state)');

promise_test(async t => {
  let controller;
  let resolvePullCalledPromise;
  const pullCalledPromise = new Promise(resolve => {
    resolvePullCalledPromise = resolve;
  });
  const stream = new ReadableStream({
    start: t.step_func((c) => {
      controller = c;
    }),
    pull: t.step_func(() => {
      resolvePullCalledPromise();
    }),
    type: 'bytes'
  });

  const reader = stream.getReader({ mode: 'byob' });
  const readPromise = reader.read(new Uint8Array([4, 5, 6]));
  await pullCalledPromise;

  // Transfer the original BYOB request's buffer, and respond with an empty view on that buffer
  const transferredView = transferArrayBufferView(controller.byobRequest.view);
  const newView = transferredView.subarray(0, 0);

  controller.close();
  controller.byobRequest.respondWithNewView(newView);

  const result = await readPromise;
  assert_true(result.done, 'result.done');

  const view = result.value;
  assert_equals(view.byteOffset, 0, 'result.value.byteOffset');
  assert_equals(view.byteLength, 0, 'result.value.byteLength');
  assert_equals(view.buffer.byteLength, 3, 'result.value.buffer.byteLength');
  assert_array_equals([...new Uint8Array(view.buffer)], [4, 5, 6], 'result.value.buffer');

}, 'ReadableStream with byte source: respondWithNewView() with a transferred zero-length view ' +
   '(in the closed state)');

promise_test(async t => {
  let controller;
  let pullCount = 0;
  const rs = new ReadableStream({
    type: 'bytes',
    autoAllocateChunkSize: 10,
    start: t.step_func((c) => {
      controller = c;
    }),
    pull: t.step_func(() => {
      ++pullCount;
    })
  });

  await flushAsyncEvents();
  assert_equals(pullCount, 0, 'pull() must not have been invoked yet');

  const reader1 = rs.getReader();
  const read1 = reader1.read();
  assert_equals(pullCount, 1, 'pull() must have been invoked once');
  const byobRequest1 = controller.byobRequest;
  assert_equals(byobRequest1.view.byteLength, 10, 'first byobRequest.view.byteLength');

  // enqueue() must discard the auto-allocated BYOB request
  controller.enqueue(new Uint8Array([1, 2, 3]));
  assert_equals(byobRequest1.view, null, 'first byobRequest must be invalidated after enqueue()');

  const result1 = await read1;
  assert_false(result1.done, 'first result.done');
  const view1 = result1.value;
  assert_equals(view1.byteOffset, 0, 'first result.value.byteOffset');
  assert_equals(view1.byteLength, 3, 'first result.value.byteLength');
  assert_array_equals([...new Uint8Array(view1.buffer)], [1, 2, 3], 'first result.value.buffer');

  reader1.releaseLock();

  // read(view) should work after discarding the auto-allocated BYOB request
  const reader2 = rs.getReader({ mode: 'byob' });
  const read2 = reader2.read(new Uint8Array([4, 5, 6]));
  assert_equals(pullCount, 2, 'pull() must have been invoked twice');
  const byobRequest2 = controller.byobRequest;
  assert_equals(byobRequest2.view.byteOffset, 0, 'second byobRequest.view.byteOffset');
  assert_equals(byobRequest2.view.byteLength, 3, 'second byobRequest.view.byteLength');
  assert_array_equals([...new Uint8Array(byobRequest2.view.buffer)], [4, 5, 6], 'second byobRequest.view.buffer');

  byobRequest2.respond(3);
  assert_equals(byobRequest2.view, null, 'second byobRequest must be invalidated after respond()');

  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  const view2 = result2.value;
  assert_equals(view2.byteOffset, 0, 'second result.value.byteOffset');
  assert_equals(view2.byteLength, 3, 'second result.value.byteLength');
  assert_array_equals([...new Uint8Array(view2.buffer)], [4, 5, 6], 'second result.value.buffer');

  reader2.releaseLock();
  assert_equals(pullCount, 2, 'pull() must only have been invoked twice');
}, 'ReadableStream with byte source: enqueue() discards auto-allocated BYOB request');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader({ mode: 'byob' });
  const read1 = reader1.read(new Uint8Array([1, 2, 3]));
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array([1, 2, 3]), 'first byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader({ mode: 'byob' });
  const read2 = reader2.read(new Uint8Array([4, 5, 6]));
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  assert_equals(controller.byobRequest, byobRequest1, 'byobRequest should be unchanged');
  assert_array_equals([...new Uint8Array(byobRequest1.view.buffer)], [1, 2, 3], 'byobRequest.view.buffer should be unchanged');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // respond() should fulfill the *second* read() request
  byobRequest1.view[0] = 11;
  byobRequest1.respond(1);
  const byobRequest2 = controller.byobRequest;
  assert_equals(byobRequest2, null, 'byobRequest should be null after respond()');

  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  assert_typed_array_equals(result2.value, new Uint8Array([11, 5, 6]).subarray(0, 1), 'second result.value');

}, 'ReadableStream with byte source: releaseLock() with pending read(view), read(view) on second reader, respond()');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader({ mode: 'byob' });
  const read1 = reader1.read(new Uint8Array([1, 2, 3]));
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array([1, 2, 3]), 'first byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader({ mode: 'byob' });
  const read2 = reader2.read(new Uint16Array(1));
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  assert_equals(controller.byobRequest, byobRequest1, 'byobRequest should be unchanged');
  assert_array_equals([...new Uint8Array(byobRequest1.view.buffer)], [1, 2, 3], 'byobRequest.view.buffer should be unchanged');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // respond(1) should partially fill the second read(), but not yet fulfill it
  byobRequest1.view[0] = 0x11;
  byobRequest1.respond(1);

  // second BYOB request should use remaining buffer from the second read()
  const byobRequest2 = controller.byobRequest;
  assert_not_equals(byobRequest2, null, 'second byobRequest should exist');
  assert_typed_array_equals(byobRequest2.view, new Uint8Array([0x11, 0]).subarray(1, 2), 'second byobRequest.view');

  // second respond(1) should fill the read request and fulfill it
  byobRequest2.view[0] = 0x22;
  byobRequest2.respond(1);
  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  const view2 = result2.value;
  assert_equals(view2.byteOffset, 0, 'second result.value.byteOffset');
  assert_equals(view2.byteLength, 2, 'second result.value.byteLength');
  const dataView2 = new DataView(view2.buffer, view2.byteOffset, view2.byteLength);
  assert_equals(dataView2.getUint16(0), 0x1122, 'second result.value[0]');

}, 'ReadableStream with byte source: releaseLock() with pending read(view), read(view) on second reader with ' +
   '1 element Uint16Array, respond(1)');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader({ mode: 'byob' });
  const read1 = reader1.read(new Uint8Array([1, 2, 3]));
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array([1, 2, 3]), 'first byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader({ mode: 'byob' });
  const read2 = reader2.read(new Uint8Array([4, 5]));
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  assert_equals(controller.byobRequest, byobRequest1, 'byobRequest should be unchanged');
  assert_array_equals([...new Uint8Array(byobRequest1.view.buffer)], [1, 2, 3], 'byobRequest.view.buffer should be unchanged');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // respond(3) should fulfill the second read(), and put 1 remaining byte in the queue
  byobRequest1.view[0] = 6;
  byobRequest1.view[1] = 7;
  byobRequest1.view[2] = 8;
  byobRequest1.respond(3);
  const byobRequest2 = controller.byobRequest;
  assert_equals(byobRequest2, null, 'byobRequest should be null after respond()');

  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  assert_typed_array_equals(result2.value, new Uint8Array([6, 7]), 'second result.value');

  // third read() should fulfill with the remaining byte
  const result3 = await reader2.read(new Uint8Array([0, 0, 0]));
  assert_false(result3.done, 'third result.done');
  assert_typed_array_equals(result3.value, new Uint8Array([8, 0, 0]).subarray(0, 1), 'third result.value');

}, 'ReadableStream with byte source: releaseLock() with pending read(view), read(view) on second reader with ' +
   '2 element Uint8Array, respond(3)');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader({ mode: 'byob' });
  const read1 = reader1.read(new Uint8Array([1, 2, 3]));
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array([1, 2, 3]), 'first byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader({ mode: 'byob' });
  const read2 = reader2.read(new Uint8Array([4, 5, 6]));
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // respondWithNewView() should fulfill the *second* read() request
  byobRequest1.view[0] = 11;
  byobRequest1.view[1] = 12;
  byobRequest1.respondWithNewView(byobRequest1.view.subarray(0, 2));
  const byobRequest2 = controller.byobRequest;
  assert_equals(byobRequest2, null, 'byobRequest should be null after respondWithNewView()');

  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  assert_typed_array_equals(result2.value, new Uint8Array([11, 12, 6]).subarray(0, 2), 'second result.value');

}, 'ReadableStream with byte source: releaseLock() with pending read(view), read(view) on second reader, respondWithNewView()');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader({ mode: 'byob' });
  const read1 = reader1.read(new Uint8Array([1, 2, 3]));
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array([1, 2, 3]), 'first byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader({ mode: 'byob' });
  const read2 = reader2.read(new Uint8Array([4, 5, 6]));
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // enqueue() should fulfill the *second* read() request
  controller.enqueue(new Uint8Array([11, 12]));
  const byobRequest2 = controller.byobRequest;
  assert_equals(byobRequest2, null, 'byobRequest should be null after enqueue()');

  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  assert_typed_array_equals(result2.value, new Uint8Array([11, 12, 6]).subarray(0, 2), 'second result.value');

}, 'ReadableStream with byte source: releaseLock() with pending read(view), read(view) on second reader, enqueue()');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader({ mode: 'byob' });
  const read1 = reader1.read(new Uint8Array([1, 2, 3]));
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array([1, 2, 3]), 'first byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader({ mode: 'byob' });
  const read2 = reader2.read(new Uint8Array([4, 5, 6]));
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // close() followed by respond(0) should fulfill the second read()
  controller.close();
  byobRequest1.respond(0);
  const byobRequest2 = controller.byobRequest;
  assert_equals(byobRequest2, null, 'byobRequest should be null after respond()');

  const result2 = await read2;
  assert_true(result2.done, 'second result.done');
  assert_typed_array_equals(result2.value, new Uint8Array([4, 5, 6]).subarray(0, 0), 'second result.value');
}, 'ReadableStream with byte source: releaseLock() with pending read(view), read(view) on second reader, ' +
   'close(), respond(0)');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    autoAllocateChunkSize: 4,
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader();
  const read1 = reader1.read();
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array(4), 'first byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader();
  const read2 = reader2.read();
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // respond() should fulfill the *second* read() request
  byobRequest1.view[0] = 11;
  byobRequest1.respond(1);
  const byobRequest2 = controller.byobRequest;
  assert_equals(byobRequest2, null, 'byobRequest should be null after respond()');

  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  assert_typed_array_equals(result2.value, new Uint8Array([11, 0, 0, 0]).subarray(0, 1), 'second result.value');

}, 'ReadableStream with byte source: autoAllocateChunkSize, releaseLock() with pending read(), read() on second reader, respond()');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    autoAllocateChunkSize: 4,
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader();
  const read1 = reader1.read();
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array(4), 'first byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader();
  const read2 = reader2.read();
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // enqueue() should fulfill the *second* read() request
  controller.enqueue(new Uint8Array([11]));
  const byobRequest2 = controller.byobRequest;
  assert_equals(byobRequest2, null, 'byobRequest should be null after enqueue()');

  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  assert_typed_array_equals(result2.value, new Uint8Array([11]), 'second result.value');

}, 'ReadableStream with byte source: autoAllocateChunkSize, releaseLock() with pending read(), read() on second reader, enqueue()');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    autoAllocateChunkSize: 4,
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader();
  const read1 = reader1.read();
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array(4), 'first byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader({ mode: 'byob' });
  const read2 = reader2.read(new Uint8Array([4, 5, 6]));
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // respond() should fulfill the *second* read() request
  byobRequest1.view[0] = 11;
  byobRequest1.respond(1);
  const byobRequest2 = controller.byobRequest;
  assert_equals(byobRequest2, null, 'byobRequest should be null after respond()');

  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  assert_typed_array_equals(result2.value, new Uint8Array([11, 5, 6]).subarray(0, 1), 'second result.value');

}, 'ReadableStream with byte source: autoAllocateChunkSize, releaseLock() with pending read(), read(view) on second reader, respond()');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    autoAllocateChunkSize: 4,
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader();
  const read1 = reader1.read();
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array(4), 'first byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader({ mode: 'byob' });
  const read2 = reader2.read(new Uint8Array([4, 5, 6]));
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // enqueue() should fulfill the *second* read() request
  controller.enqueue(new Uint8Array([11]));
  const byobRequest2 = controller.byobRequest;
  assert_equals(byobRequest2, null, 'byobRequest should be null after enqueue()');

  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  assert_typed_array_equals(result2.value, new Uint8Array([11, 5, 6]).subarray(0, 1), 'second result.value');

}, 'ReadableStream with byte source: autoAllocateChunkSize, releaseLock() with pending read(), read(view) on second reader, enqueue()');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader({ mode: 'byob' });
  const read1 = reader1.read(new Uint16Array(1));
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array([0, 0]), 'first byobRequest.view');

  // respond(1) should partially fill the first read(), but not yet fulfill it
  byobRequest1.view[0] = 0x11;
  byobRequest1.respond(1);
  const byobRequest2 = controller.byobRequest;
  assert_not_equals(byobRequest2, null, 'second byobRequest should exist');
  assert_typed_array_equals(byobRequest2.view, new Uint8Array([0x11, 0]).subarray(1, 2), 'second byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader({ mode: 'byob' });
  const read2 = reader2.read(new Uint16Array(1));
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  assert_equals(controller.byobRequest, byobRequest2, 'byobRequest should be unchanged');
  assert_typed_array_equals(byobRequest2.view, new Uint8Array([0x11, 0]).subarray(1, 2), 'byobRequest.view should be unchanged');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // second respond(1) should fill the read request and fulfill it
  byobRequest2.view[0] = 0x22;
  byobRequest2.respond(1);
  assert_equals(controller.byobRequest, null, 'byobRequest should be invalidated after second respond()');

  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  const view2 = result2.value;
  assert_equals(view2.byteOffset, 0, 'second result.value.byteOffset');
  assert_equals(view2.byteLength, 2, 'second result.value.byteLength');
  const dataView2 = new DataView(view2.buffer, view2.byteOffset, view2.byteLength);
  assert_equals(dataView2.getUint16(0), 0x1122, 'second result.value[0]');

}, 'ReadableStream with byte source: read(view) with 1 element Uint16Array, respond(1), releaseLock(), read(view) on ' +
   'second reader with 1 element Uint16Array, respond(1)');

promise_test(async t => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start: t.step_func((c) => {
      controller = c;
    })
  });
  await flushAsyncEvents();

  const reader1 = rs.getReader({ mode: 'byob' });
  const read1 = reader1.read(new Uint16Array(1));
  const byobRequest1 = controller.byobRequest;
  assert_not_equals(byobRequest1, null, 'first byobRequest should exist');
  assert_typed_array_equals(byobRequest1.view, new Uint8Array([0, 0]), 'first byobRequest.view');

  // respond(1) should partially fill the first read(), but not yet fulfill it
  byobRequest1.view[0] = 0x11;
  byobRequest1.respond(1);
  const byobRequest2 = controller.byobRequest;
  assert_not_equals(byobRequest2, null, 'second byobRequest should exist');
  assert_typed_array_equals(byobRequest2.view, new Uint8Array([0x11, 0]).subarray(1, 2), 'second byobRequest.view');

  // releaseLock() should reject the pending read, but *not* invalidate the BYOB request
  reader1.releaseLock();
  const reader2 = rs.getReader();
  const read2 = reader2.read();
  assert_not_equals(controller.byobRequest, null, 'byobRequest should not be invalidated after releaseLock()');
  assert_equals(controller.byobRequest, byobRequest2, 'byobRequest should be unchanged');
  assert_typed_array_equals(byobRequest2.view, new Uint8Array([0x11, 0]).subarray(1, 2), 'byobRequest.view should be unchanged');
  await promise_rejects_js(t, TypeError, read1, 'pending read must reject after releaseLock()');

  // enqueue() should fulfill the read request and put remaining byte in the queue
  controller.enqueue(new Uint8Array([0x22]));
  assert_equals(controller.byobRequest, null, 'byobRequest should be invalidated after second respond()');

  const result2 = await read2;
  assert_false(result2.done, 'second result.done');
  assert_typed_array_equals(result2.value, new Uint8Array([0x11]), 'second result.value');

  const result3 = await reader2.read();
  assert_false(result3.done, 'third result.done');
  assert_typed_array_equals(result3.value, new Uint8Array([0x22]), 'third result.value');

}, 'ReadableStream with byte source: read(view) with 1 element Uint16Array, respond(1), releaseLock(), read() on ' +
   'second reader, enqueue()');

promise_test(async t => {
  // Tests https://github.com/nodejs/node/issues/41886
  const stream = new ReadableStream({
    type: 'bytes',
    autoAllocateChunkSize: 10,
    pull: t.step_func((c) => {
      const newView = new Uint8Array(c.byobRequest.view.buffer, 0, 3);
      newView.set([20, 21, 22]);
      c.byobRequest.respondWithNewView(newView);
    })
  });

  const reader = stream.getReader();
  const result = await reader.read();
  assert_false(result.done, 'result.done');

  const view = result.value;
  assert_equals(view.byteOffset, 0, 'result.value.byteOffset');
  assert_equals(view.byteLength, 3, 'result.value.byteLength');
  assert_equals(view.buffer.byteLength, 10, 'result.value.buffer.byteLength');
  assert_array_equals([...new Uint8Array(view)], [20, 21, 22], 'result.value');
}, 'ReadableStream with byte source: autoAllocateChunkSize, read(), respondWithNewView()');
