// META: global=window,worker,shadowrealm

'use strict';

// This test asserts that compressed data length is shorter than the original
// data length. If the input is extremely small, the compressed data may be
// larger than the original data.

const LARGE_FILE = '/media/test-av-384k-44100Hz-1ch-320x240-30fps-10kfr.webm';

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

promise_test(async () => {
  const response = await fetch(LARGE_FILE);
  const buffer = await response.arrayBuffer();
  const bufferView = new Uint8Array(buffer);
  const originalLength = bufferView.length;
  const compressedData = await compressArrayBuffer(bufferView, 'deflate');
  const compressedLength = compressedData.length;
  assert_less_than(compressedLength, originalLength, 'output should be smaller');
}, 'the length of deflated data should be shorter than that of the original data');

promise_test(async () => {
  const response = await fetch(LARGE_FILE);
  const buffer = await response.arrayBuffer();
  const bufferView = new Uint8Array(buffer);
  const originalLength = bufferView.length;
  const compressedData = await compressArrayBuffer(bufferView, 'gzip');
  const compressedLength = compressedData.length;
  assert_less_than(compressedLength, originalLength, 'output should be smaller');
}, 'the length of gzipped data should be shorter than that of the original data');

promise_test(async () => {
  const response = await fetch(LARGE_FILE);
  const buffer = await response.arrayBuffer();
  const bufferView = new Uint8Array(buffer);
  const originalLength = bufferView.length;
  const compressedData = await compressArrayBuffer(bufferView, 'deflate-raw');
  const compressedLength = compressedData.length;
  assert_less_than(compressedLength, originalLength, 'output should be smaller');
}, 'the length of deflated (with -raw) data should be shorter than that of the original data');
