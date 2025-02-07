// Copyright 2012 the V8 project authors. All rights reserved.
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

// Create elements in a constructor function to ensure map sharing.
function TestConstructor() {
  this[0] = 1;
  this[1] = 2;
  this[2] = 3;
}

function bad_func(o, a) {
  var s = 0;
  for (var i = 0; i < 1; ++i) {
    o.newFileToChangeMap = undefined;
    var x = a[0];
    s += x;
  }
  return s;
};
%PrepareFunctionForOptimization(bad_func);
o = new Object();
a = new TestConstructor();
bad_func(o, a);

// Make sure that we're out of pre-monomorphic state for the member add of
// 'newFileToChangeMap' which causes a map transition.
o = new Object();
a = new TestConstructor();
bad_func(o, a);

// Optimize, before the fix, the element load and subsequent tagged-to-i were
// hoisted above the map check, which can't be hoisted due to the map-changing
// store.
o = new Object();
a = new TestConstructor();
%OptimizeFunctionOnNextCall(bad_func);
bad_func(o, a);

// Pass in an array of doubles. Before the fix, the optimized load and
// tagged-to-i will treat part of a double value as a pointer and de-ref it
// before the map check was executed that should have deopt.
o = new Object();
// Pass in an elements buffer where the bit representation of the double numbers
// are two adjacent small 32-bit values with the lowest bit set to one, causing
// tagged-to-i to SIGSEGV.
a = [2.122e-314, 2.122e-314, 2.122e-314];
bad_func(o, a);
