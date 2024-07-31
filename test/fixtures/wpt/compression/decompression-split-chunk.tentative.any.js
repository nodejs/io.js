// META: global=window,worker,shadowrealm

'use strict';

const compressedBytesWithDeflate = new Uint8Array([120, 156, 75, 173, 40, 72, 77, 46, 73, 77, 81, 200, 47, 45, 41, 40, 45, 1, 0, 48, 173, 6, 36]);
const compressedBytesWithGzip = new Uint8Array([31, 139, 8, 0, 0, 0, 0, 0, 0, 3, 75, 173, 40, 72, 77, 46, 73, 77, 81, 200, 47, 45, 41, 40, 45, 1, 0, 176, 1, 57, 179, 15, 0, 0, 0]);
const compressedBytesWithDeflateRaw = new Uint8Array([
  0x4b, 0xad, 0x28, 0x48, 0x4d, 0x2e, 0x49, 0x4d, 0x51, 0xc8,
  0x2f, 0x2d, 0x29, 0x28, 0x2d, 0x01, 0x00,
]);
const expectedChunkValue = new TextEncoder().encode('expected output');

async function decompressArrayBuffer(input, format, chunkSize) {
  const ds = new DecompressionStream(format);
  const reader = ds.readable.getReader();
  const writer = ds.writable.getWriter();
  for (let beginning = 0; beginning < input.length; beginning += chunkSize) {
    writer.write(input.slice(beginning, beginning + chunkSize));
  }
  writer.close();
  const out = [];
  let totalSize = 0;
  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    out.push(value);
    totalSize += value.byteLength;
  }
  const concatenated = new Uint8Array(totalSize);
  let offset = 0;
  for (const array of out) {
    concatenated.set(array, offset);
    offset += array.byteLength;
  }
  return concatenated;
}

for (let chunkSize = 1; chunkSize < 16; ++chunkSize) {
  promise_test(async t => {
    const decompressedData = await decompressArrayBuffer(compressedBytesWithDeflate, 'deflate', chunkSize);
    assert_array_equals(decompressedData, expectedChunkValue, "value should match");
  }, `decompressing splitted chunk into pieces of size ${chunkSize} should work in deflate`);

  promise_test(async t => {
    const decompressedData = await decompressArrayBuffer(compressedBytesWithGzip, 'gzip', chunkSize);
    assert_array_equals(decompressedData, expectedChunkValue, "value should match");
  }, `decompressing splitted chunk into pieces of size ${chunkSize} should work in gzip`);

  promise_test(async t => {
    const decompressedData = await decompressArrayBuffer(compressedBytesWithDeflateRaw, 'deflate-raw', chunkSize);
    assert_array_equals(decompressedData, expectedChunkValue, "value should match");
  }, `decompressing splitted chunk into pieces of size ${chunkSize} should work in deflate-raw`);
}
