// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Flags: --allow-natives-syntax --mock-arraybuffer-allocator

let kArrayBufferByteLengthLimit = %ArrayBufferMaxByteLength() + 1;

// Allocate the largest ArrayBuffer we can on this architecture.
let ab = new ArrayBuffer(kArrayBufferByteLengthLimit - 1);

function TestArray(constr, elementSize) {
  assertEquals(elementSize, constr.BYTES_PER_ELEMENT);
  assertEquals(kArrayBufferByteLengthLimit % elementSize, 0);
  let ta_limit = kArrayBufferByteLengthLimit / elementSize;

  assertThrows(() => new constr(ab, 0, ta_limit), RangeError);

  // This one must succeed.
  new constr(ab, 0, ta_limit - 1);
}

TestArray(Uint8Array, 1);
TestArray(Int8Array, 1);
TestArray(Uint16Array, 2);
TestArray(Int16Array, 2);
TestArray(Uint32Array, 4);
TestArray(Int32Array, 4);
TestArray(Float32Array, 4);
TestArray(Float64Array, 8);
TestArray(Uint8ClampedArray, 1);
