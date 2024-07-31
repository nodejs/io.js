// Copyright 2011 the V8 project authors. All rights reserved.
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

// Flags: --allow-natives-syntax
// Flags: --js-float16array

// This is a regression test for overlapping key and value registers.

var types = [Array, Int8Array, Uint8Array, Int16Array, Uint16Array,
             Int32Array, Uint32Array, Uint8ClampedArray, Float16Array, Float32Array,
             Float64Array];

var results1 = [-2, -2, 254, -2, 65534, -2, 4294967294, 0, -2, -2, -2];
var results2 = [undefined, -1, 255, -1, 65535, -1, 4294967295, 0, -1, -1, -1];
var results3 = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
var results4 = [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1];

const kElementCount = 40;

function do_slice(a) {
  return Array.prototype.slice.call(a, 4, 8);
}

for (var t = 0; t < types.length; t++) {
  var type = types[t];
  var a = new type(kElementCount);
  for (var i = 0; i < kElementCount; ++i ) {
    a[i] = i-6;
  }
  delete a[5];
  var sliced = do_slice(a);

  %ClearFunctionFeedback(do_slice);
  assertEquals(results1[t], sliced[0]);
  assertEquals(results2[t], sliced[1]);
  assertEquals(results3[t], sliced[2]);
  assertEquals(results4[t], sliced[3]);
}
