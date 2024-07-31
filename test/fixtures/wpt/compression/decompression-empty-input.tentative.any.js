// META: global=window,worker,shadowrealm

'use strict';

const gzipEmptyValue = new Uint8Array([31, 139, 8, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
const deflateEmptyValue = new Uint8Array([120, 156, 3, 0, 0, 0, 0, 1]);
const deflateRawEmptyValue = new Uint8Array([1, 0, 0, 255, 255]);

promise_test(async t => {
  const ds = new DecompressionStream('gzip');
  const reader = ds.readable.getReader();
  const writer = ds.writable.getWriter();
  const writePromise = writer.write(gzipEmptyValue);
  writer.close();
  const { value, done } = await reader.read();
  assert_true(done, "read() should set done");
  assert_equals(value, undefined, "value should be undefined");
  await writePromise;
}, 'decompressing gzip empty input should work');

promise_test(async t => {
  const ds = new DecompressionStream('deflate');
  const reader = ds.readable.getReader();
  const writer = ds.writable.getWriter();
  const writePromise = writer.write(deflateEmptyValue);
  writer.close();
  const { value, done } = await reader.read();
  assert_true(done, "read() should set done");
  assert_equals(value, undefined, "value should be undefined");
  await writePromise;
}, 'decompressing deflate empty input should work');

promise_test(async t => {
  const ds = new DecompressionStream('deflate-raw');
  const reader = ds.readable.getReader();
  const writer = ds.writable.getWriter();
  const writePromise = writer.write(deflateRawEmptyValue);
  writer.close();
  const { value, done } = await reader.read();
  assert_true(done, "read() should set done");
  assert_equals(value, undefined, "value should be undefined");
  await writePromise;
}, 'decompressing deflate-raw empty input should work');
