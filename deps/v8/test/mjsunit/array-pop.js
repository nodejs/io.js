// Copyright 2010 the V8 project authors. All rights reserved.
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

// Test that elements are replaced with holes.
// This test must be the first test to guarantee elements protectors are valid.
(function() {
  function pop(arr) {
    return arr.pop();
  }
  let smi_arr = [1, 2, 3, 4];
  let object_arr = [{}, {}, {}, {}];
  let double_arr = [1.1, 2.2, 3.3, 4.4];
  %PrepareFunctionForOptimization(pop);
  assertEquals(4, pop(smi_arr));
  assertEquals({}, pop(object_arr));
  assertEquals(4.4, pop(double_arr));
  assertEquals(3, pop(smi_arr));
  assertEquals({}, pop(object_arr));
  assertEquals(3.3, pop(double_arr));
  %OptimizeFunctionOnNextCall(pop);
  assertEquals(2, pop(smi_arr));
  assertEquals({}, pop(object_arr));
  assertEquals(2.2, pop(double_arr));
  smi_arr[2] = 3;
  object_arr[2] = {};
  double_arr[2] = 3.3;
  assertEquals(undefined, smi_arr[1]);
  assertEquals(undefined, object_arr[1]);
  assertEquals(undefined, double_arr[1]);
})();

// Check pops with various number of arguments.
(function() {
  var a = [];
  for (var i = 0; i < 7; i++) {
    a = [7, 6, 5, 4, 3, 2, 1];

    assertEquals(1, a.pop(), "1st pop");
    assertEquals(6, a.length, "length 1st pop");

    assertEquals(2, a.pop(1), "2nd pop");
    assertEquals(5, a.length, "length 2nd pop");

    assertEquals(3, a.pop(1, 2), "3rd pop");
    assertEquals(4, a.length, "length 3rd pop");

    assertEquals(4, a.pop(1, 2, 3), "4th pop");
    assertEquals(3, a.length, "length 4th pop");

    assertEquals(5, a.pop(), "5th pop");
    assertEquals(2, a.length, "length 5th pop");

    assertEquals(6, a.pop(), "6th pop");
    assertEquals(1, a.length, "length 6th pop");

    assertEquals(7, a.pop(), "7th pop");
    assertEquals(0, a.length, "length 7th pop");

    assertEquals(undefined, a.pop(), "8th pop");
    assertEquals(0, a.length, "length 8th pop");

    assertEquals(undefined, a.pop(1, 2, 3), "9th pop");
    assertEquals(0, a.length, "length 9th pop");
  }

  // Check that pop works on inherited properties.
  for (var i = 0; i < 10 ;i++) {  // Ensure ICs are stabilized.
    Array.prototype[1] = 1;
    Array.prototype[3] = 3;
    Array.prototype[5] = 5;
    Array.prototype[7] = 7;
    Array.prototype[9] = 9;
    a = [0,1,2,,4,,6,7,8,,];
    assertEquals(10, a.length, "inherit-initial-length");
    for (var j = 9; j >= 0; j--) {
      assertEquals(j + 1, a.length, "inherit-pre-length-" + j);
      assertTrue(j in a, "has property " + j);
      var own = a.hasOwnProperty(j);
      var inherited = Array.prototype.hasOwnProperty(j);
      assertEquals(j, a.pop(), "inherit-pop");
      assertEquals(j, a.length, "inherit-post-length");
      assertFalse(a.hasOwnProperty(j), "inherit-deleted-own-" + j);
      assertEquals(inherited, Array.prototype.hasOwnProperty(j),
                   "inherit-not-deleted-inherited" + j);
    }
    Array.prototype.length = 0;  // Clean-up.
  }

  // Check that pop works on inherited properties for
  // arrays with array prototype.
  for (var i = 0; i < 10 ;i++) {  // Ensure ICs are stabilized.
    var array_proto = [];
    array_proto[1] = 1;
    array_proto[3] = 3;
    array_proto[5] = 5;
    array_proto[7] = 7;
    array_proto[9] = 9;
    a = [0,1,2,,4,,6,7,8,,];
    a.__proto__ = array_proto;
    assertEquals(10, a.length, "array_proto-inherit-initial-length");
    for (var j = 9; j >= 0; j--) {
      assertEquals(j + 1, a.length, "array_proto-inherit-pre-length-" + j);
      assertTrue(j in a, "array_proto-has property " + j);
      var own = a.hasOwnProperty(j);
      var inherited = array_proto.hasOwnProperty(j);
      assertEquals(j, a.pop(), "array_proto-inherit-pop");
      assertEquals(j, a.length, "array_proto-inherit-post-length");
      assertFalse(a.hasOwnProperty(j), "array_proto-inherit-deleted-own-" + j);
      assertEquals(inherited, array_proto.hasOwnProperty(j),
                   "array_proto-inherit-not-deleted-inherited" + j);
    }
  }

  // Check that pop works on inherited properties for
  // arrays with array prototype.
})();

// Test the case of not JSArray receiver.
// Regression test for custom call generators, see issue 684.
(function() {
  var a = [];
  for (var i = 0; i < 100; i++) a.push(i);
  var x = {__proto__: a};
  for (var i = 0; i < 100; i++) {
    assertEquals(99 - i, x.pop(), i + 'th iteration');
  }
})();

(function () {
  function f(a, deopt) {
    var v = a.pop() ? 1 : 2;
    if (deopt) %DeoptimizeFunction(f);
    return v;
  }

  %PrepareFunctionForOptimization(f);
  var a = [true, true, true, true]
  assertEquals(1, f(a, false));
  assertEquals(1, f(a, false));
  %OptimizeFunctionOnNextCall(f);
  assertEquals(1, f(a, false));
  assertEquals(1, f(a, true));
})();
