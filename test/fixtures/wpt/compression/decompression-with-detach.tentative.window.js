// META: global=window,worker,shadowrealm
// META: script=resources/concatenate-stream.js

'use strict';

const kInputLength = 1000000;

async function createLargeCompressedInput() {
  const cs = new CompressionStream('deflate');
  // The input has to be large enough that it won't fit in a single chunk when
  // decompressed.
  const writer = cs.writable.getWriter();
  writer.write(new Uint8Array(kInputLength));
  writer.close();
  return concatenateStream(cs.readable);
}

promise_test(async () => {
  const input = await createLargeCompressedInput();
  const ds = new DecompressionStream('deflate');
  const writer = ds.writable.getWriter();
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
  const output = await concatenateStream(ds.readable);
  // If output successfully decompressed and gave the right length, we can be
  // reasonably confident that no data corruption happened.
  assert_equals(
      output.byteLength, kInputLength, 'output should be the right length');
}, 'data should be correctly decompressed even if input is detached partway');
