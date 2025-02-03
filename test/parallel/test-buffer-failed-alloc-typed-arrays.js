'use strict';

require('../common');
const assert = require('assert');
const SlowBuffer = require('buffer').SlowBuffer;

// Test failed or zero-sized Buffer allocations not affecting typed arrays.
// This test exists because of a regression that occurred. Because Buffer
// instances are allocated with the same underlying allocator as TypedArrays,
// but Buffer's can optional be non-zero filled, there was a regression that
// occurred when a Buffer allocated failed, the internal flag specifying
// whether or not to zero-fill was not being reset, causing TypedArrays to
// allocate incorrectly.
const zeroArray = new Uint32Array(10).fill(0);
const sizes = [1e20, 0, 0.1, -1, 'a', undefined, null, NaN];
const allocators = [
  Buffer,
  SlowBuffer,
  Buffer.alloc,
  Buffer.allocUnsafe,
  Buffer.allocUnsafeSlow,
];
for (const allocator of allocators) {
  for (const size of sizes) {
    try {
      // Some of these allocations are known to fail. If they do,
      // Uint32Array should still produce a zeroed out result.
      allocator(size);
    } catch {
      assert.deepStrictEqual(zeroArray, new Uint32Array(10));
    }
  }
}
