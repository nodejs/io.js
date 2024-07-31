// META: global=window,worker,shadowrealm
// META: script=resources/concatenate-stream.js

'use strict';

const kInputLength = 500000;

function createLargeRandomInput() {
  const buffer = new ArrayBuffer(kInputLength);
  // The getRandomValues API will only let us get 65536 bytes at a time, so call
  // it multiple times.
  const kChunkSize = 65536;
  for (let offset = 0; offset < kInputLength; offset += kChunkSize) {
    const length =
        offset + kChunkSize > kInputLength ? kInputLength - offset : kChunkSize;
    const view = new Uint8Array(buffer, offset, length);
    crypto.getRandomValues(view);
  }
  return new Uint8Array(buffer);
}

function decompress(view) {
  const ds = new DecompressionStream('deflate');
  const writer = ds.writable.getWriter();
  writer.write(view);
  writer.close();
  return concatenateStream(ds.readable);
}

promise_test(async () => {
  const input = createLargeRandomInput();
  const inputCopy = input.slice(0, input.byteLength);
  const cs = new CompressionStream('deflate');
  const writer = cs.writable.getWriter();
  writer.write(input);
  writer.close();
  // Object.prototype.then will be looked up synchronously when the promise
  // returned by read() is resolved.
  Object.defineProperty(Object.prototype, 'then', {
    get() {
      // Cause input to become detached and unreferenced.
      try {
        postMessage(undefined, 'nowhere', [input.buffer]);
      } catch (e) {
        // It's already detached.
      }
    }
  });
  const output = await concatenateStream(cs.readable);
  // Perform the comparison as strings since this is reasonably fast even when
  // JITted JavaScript is running under an emulator.
  assert_equals(
      inputCopy.toString(), (await decompress(output)).toString(),
      'decompressing the output should return the input');
}, 'data should be correctly compressed even if input is detached partway');
