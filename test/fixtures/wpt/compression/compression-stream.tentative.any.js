// META: global=window,worker,shadowrealm
// META: script=third_party/pako/pako_inflate.min.js
// META: timeout=long

'use strict';

const SMALL_FILE = "/media/foo.vtt";
const LARGE_FILE = "/media/test-av-384k-44100Hz-1ch-320x240-30fps-10kfr.webm";

async function compressArrayBuffer(input, format) {
  const cs = new CompressionStream(format);
  const writer = cs.writable.getWriter();
  writer.write(input);
  const closePromise = writer.close();
  const out = [];
  const reader = cs.readable.getReader();
  let totalSize = 0;
  while (true) {
    const { value, done } = await reader.read();
    if (done)
      break;
    out.push(value);
    totalSize += value.byteLength;
  }
  await closePromise;
  const concatenated = new Uint8Array(totalSize);
  let offset = 0;
  for (const array of out) {
    concatenated.set(array, offset);
    offset += array.byteLength;
  }
  return concatenated;
}

test(() => {
  assert_throws_js(TypeError, () => {
    const transformer = new CompressionStream("nonvalid");
  }, "non supported format should throw");
}, "CompressionStream constructor should throw on invalid format");

promise_test(async () => {
  const buffer = new ArrayBuffer(0);
  const bufferView = new Uint8Array(buffer);
  const compressedData = await compressArrayBuffer(bufferView, "deflate");
  // decompress with pako, and check that we got the same result as our original string
  assert_array_equals(bufferView, pako.inflate(compressedData));
}, "deflated empty data should be reinflated back to its origin");

promise_test(async () => {
  const response = await fetch(SMALL_FILE)
  const buffer = await response.arrayBuffer();
  const bufferView = new Uint8Array(buffer);
  const compressedData = await compressArrayBuffer(bufferView, "deflate");
  // decompress with pako, and check that we got the same result as our original string
  assert_array_equals(bufferView, pako.inflate(compressedData));
}, "deflated small amount data should be reinflated back to its origin");

promise_test(async () => {
  const response = await fetch(LARGE_FILE)
  const buffer = await response.arrayBuffer();
  const bufferView = new Uint8Array(buffer);
  const compressedData = await compressArrayBuffer(bufferView, "deflate");
  // decompress with pako, and check that we got the same result as our original string
  assert_array_equals(bufferView, pako.inflate(compressedData));
}, "deflated large amount data should be reinflated back to its origin");

promise_test(async () => {
  const buffer = new ArrayBuffer(0);
  const bufferView = new Uint8Array(buffer);
  const compressedData = await compressArrayBuffer(bufferView, "gzip");
  // decompress with pako, and check that we got the same result as our original string
  assert_array_equals(bufferView, pako.inflate(compressedData));
}, "gzipped empty data should be reinflated back to its origin");

promise_test(async () => {
  const response = await fetch(SMALL_FILE)
  const buffer = await response.arrayBuffer();
  const bufferView = new Uint8Array(buffer);
  const compressedData = await compressArrayBuffer(bufferView, "gzip");
  // decompress with pako, and check that we got the same result as our original string
  assert_array_equals(bufferView, pako.inflate(compressedData));
}, "gzipped small amount data should be reinflated back to its origin");

promise_test(async () => {
  const response = await fetch(LARGE_FILE)
  const buffer = await response.arrayBuffer();
  const bufferView = new Uint8Array(buffer);
  const compressedData = await compressArrayBuffer(bufferView, "gzip");
  // decompress with pako, and check that we got the same result as our original string
  assert_array_equals(bufferView, pako.inflate(compressedData));
}, "gzipped large amount data should be reinflated back to its origin");
