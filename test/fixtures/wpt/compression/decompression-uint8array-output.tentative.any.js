// META: global=window,worker,shadowrealm
// META: timeout=long
//
// This test isn't actually slow usually, but sometimes it takes >10 seconds on
// Firefox with service worker for no obvious reason.

'use strict';

const deflateChunkValue = new Uint8Array([120, 156, 75, 173, 40, 72, 77, 46, 73, 77, 81, 200, 47, 45, 41, 40, 45, 1, 0, 48, 173, 6, 36]);
const gzipChunkValue = new Uint8Array([31, 139, 8, 0, 0, 0, 0, 0, 0, 3, 75, 173, 40, 72, 77, 46, 73, 77, 81, 200, 47, 45, 41, 40, 45, 1, 0, 176, 1, 57, 179, 15, 0, 0, 0]);

promise_test(async t => {
  const ds = new DecompressionStream('deflate');
  const reader = ds.readable.getReader();
  const writer = ds.writable.getWriter();
  const writePromise = writer.write(deflateChunkValue);
  const { value } = await reader.read();
  assert_equals(value.constructor, Uint8Array, "type should match");
  await writePromise;
}, 'decompressing deflated output should give Uint8Array chunks');

promise_test(async t => {
  const ds = new DecompressionStream('gzip');
  const reader = ds.readable.getReader();
  const writer = ds.writable.getWriter();
  const writePromise = writer.write(gzipChunkValue);
  const { value } = await reader.read();
  assert_equals(value.constructor, Uint8Array, "type should match");
  await writePromise;
}, 'decompressing gzip output should give Uint8Array chunks');
