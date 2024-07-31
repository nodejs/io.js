// META: global=window,worker,shadowrealm

'use strict';

const badChunks = [
  {
    name: 'undefined',
    value: undefined
  },
  {
    name: 'null',
    value: null
  },
  {
    name: 'numeric',
    value: 3.14
  },
  {
    name: 'object, not BufferSource',
    value: {}
  },
  {
    name: 'array',
    value: [65]
  },
  {
    name: 'SharedArrayBuffer',
    // Use a getter to postpone construction so that all tests don't fail where
    // SharedArrayBuffer is not yet implemented.
    get value() {
      // See https://github.com/whatwg/html/issues/5380 for why not `new SharedArrayBuffer()`
      return new WebAssembly.Memory({ shared:true, initial:1, maximum:1 }).buffer;
    }
  },
  {
    name: 'shared Uint8Array',
    get value() {
      // See https://github.com/whatwg/html/issues/5380 for why not `new SharedArrayBuffer()`
      return new Uint8Array(new WebAssembly.Memory({ shared:true, initial:1, maximum:1 }).buffer)
    }
  },
];

for (const chunk of badChunks) {
  promise_test(async t => {
    const cs = new CompressionStream('gzip');
    const reader = cs.readable.getReader();
    const writer = cs.writable.getWriter();
    const writePromise = writer.write(chunk.value);
    const readPromise = reader.read();
    await promise_rejects_js(t, TypeError, writePromise, 'write should reject');
    await promise_rejects_js(t, TypeError, readPromise, 'read should reject');
  }, `chunk of type ${chunk.name} should error the stream for gzip`);

  promise_test(async t => {
    const cs = new CompressionStream('deflate');
    const reader = cs.readable.getReader();
    const writer = cs.writable.getWriter();
    const writePromise = writer.write(chunk.value);
    const readPromise = reader.read();
    await promise_rejects_js(t, TypeError, writePromise, 'write should reject');
    await promise_rejects_js(t, TypeError, readPromise, 'read should reject');
  }, `chunk of type ${chunk.name} should error the stream for deflate`);

  promise_test(async t => {
    const cs = new CompressionStream('deflate-raw');
    const reader = cs.readable.getReader();
    const writer = cs.writable.getWriter();
    const writePromise = writer.write(chunk.value);
    const readPromise = reader.read();
    await promise_rejects_js(t, TypeError, writePromise, 'write should reject');
    await promise_rejects_js(t, TypeError, readPromise, 'read should reject');
  }, `chunk of type ${chunk.name} should error the stream for deflate-raw`);
}
