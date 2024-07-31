// META: global=window,worker,shadowrealm
'use strict';

promise_test(async t => {
  const rs = new ReadableStream({
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = rs.getReader({ mode: 'byob' });
  const memory = new WebAssembly.Memory({ initial: 1 });
  const view = new Uint8Array(memory.buffer, 0, 1);
  await promise_rejects_js(t, TypeError, reader.read(view));
}, 'ReadableStream with byte source: read() with a non-transferable buffer');

promise_test(async t => {
  const rs = new ReadableStream({
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const reader = rs.getReader({ mode: 'byob' });
  const memory = new WebAssembly.Memory({ initial: 1 });
  const view = new Uint8Array(memory.buffer, 0, 1);
  await promise_rejects_js(t, TypeError, reader.read(view, { min: 1 }));
}, 'ReadableStream with byte source: fill() with a non-transferable buffer');

test(t => {
  let controller;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull: t.unreached_func('pull() should not be called'),
    type: 'bytes'
  });

  const memory = new WebAssembly.Memory({ initial: 1 });
  const view = new Uint8Array(memory.buffer, 0, 1);
  assert_throws_js(TypeError, () => controller.enqueue(view));
}, 'ReadableStream with byte source: enqueue() with a non-transferable buffer');

promise_test(async t => {
  let byobRequest;
  let resolvePullCalledPromise;
  const pullCalledPromise = new Promise(resolve => {
    resolvePullCalledPromise = resolve;
  });
  const rs = new ReadableStream({
    pull(controller) {
      byobRequest = controller.byobRequest;
      resolvePullCalledPromise();
    },
    type: 'bytes'
  });

  const memory = new WebAssembly.Memory({ initial: 1 });
  // Make sure the backing buffers of both views have the same length
  const byobView = new Uint8Array(new ArrayBuffer(memory.buffer.byteLength), 0, 1);
  const newView = new Uint8Array(memory.buffer, byobView.byteOffset, byobView.byteLength);

  const reader = rs.getReader({ mode: 'byob' });
  reader.read(byobView).then(
    t.unreached_func('read() should not resolve'),
    t.unreached_func('read() should not reject')
  );
  await pullCalledPromise;

  assert_throws_js(TypeError, () => byobRequest.respondWithNewView(newView));
}, 'ReadableStream with byte source: respondWithNewView() with a non-transferable buffer');
