// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --mock-arraybuffer-allocator --allow-natives-syntax

function Module(stdlib, foreign, buffer) {
  "use asm";
  var heap = new stdlib.Int8Array(buffer);
  function foo() { return heap[23] | 0 }
  return { foo:foo };
}
const kLength = Math.max(0x100000000, %ArrayBufferMaxByteLength() + 1);
function instantiate() {
  // On 32-bit architectures buffer allocation will throw.
  var buffer = new ArrayBuffer(kLength);
  // On 64-bit architectures instantiation will throw.
  var module = Module(this, {}, buffer);
}
assertThrows(instantiate, RangeError);
