// META: global=window,worker,shadowrealm
// META: script=third_party/pako/pako_inflate.min.js
// META: timeout=long

'use strict';

// This test asserts that compressing multiple chunks should work.

// Example: ('Hello', 3) => TextEncoder().encode('HelloHelloHello')
function makeExpectedChunk(input, numberOfChunks) {
  const expectedChunk = input.repeat(numberOfChunks);
  return new TextEncoder().encode(expectedChunk);
}

// Example: ('Hello', 3, 'deflate') => compress ['Hello', 'Hello', Hello']
async function compressMultipleChunks(input, numberOfChunks, format) {
  const cs = new CompressionStream(format);
  const writer = cs.writable.getWriter();
  const chunk = new TextEncoder().encode(input);
  for (let i = 0; i < numberOfChunks; ++i) {
    writer.write(chunk);
  }
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

const hello = 'Hello';

for (let numberOfChunks = 2; numberOfChunks <= 16; ++numberOfChunks) {
  promise_test(async t => {
    const compressedData = await compressMultipleChunks(hello, numberOfChunks, 'deflate');
    const expectedValue = makeExpectedChunk(hello, numberOfChunks);
    // decompress with pako, and check that we got the same result as our original string
    assert_array_equals(expectedValue, pako.inflate(compressedData), 'value should match');
  }, `compressing ${numberOfChunks} chunks with deflate should work`);

  promise_test(async t => {
    const compressedData = await compressMultipleChunks(hello, numberOfChunks, 'gzip');
    const expectedValue = makeExpectedChunk(hello, numberOfChunks);
    // decompress with pako, and check that we got the same result as our original string
    assert_array_equals(expectedValue, pako.inflate(compressedData), 'value should match');
  }, `compressing ${numberOfChunks} chunks with gzip should work`);

  promise_test(async t => {
    const compressedData = await compressMultipleChunks(hello, numberOfChunks, 'deflate-raw');
    const expectedValue = makeExpectedChunk(hello, numberOfChunks);
    // decompress with pako, and check that we got the same result as our original string
    assert_array_equals(expectedValue, pako.inflateRaw(compressedData), 'value should match');
  }, `compressing ${numberOfChunks} chunks with deflate-raw should work`);
}
