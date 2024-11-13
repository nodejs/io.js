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

assertTrue(isNaN(Math.hypot({})));
assertTrue(isNaN(Math.hypot(undefined, 1)));
assertTrue(isNaN(Math.hypot(1, undefined)));
assertTrue(isNaN(Math.hypot(Math.hypot, 1)));
assertEquals(1, Math.hypot(1));
assertEquals(Math.PI, Math.hypot(Math.PI));
assertEquals(5, Math.hypot(3, 4));
assertEquals(13, Math.hypot(3, 4, 12));
assertEquals(15, Math.hypot(" 2 ",
                            "0x5",
                            { valueOf: function() { return "0xe"; } }));
assertEquals(17, Math.hypot({ valueOf: function() { return 1; } },
                            { toString: function() { return 12; } },
                            { toString: function() { return "12"; } }));

// Check function length.
assertEquals(2, Math.hypot.length);

// Check that 0 is returned for no arguments.
assertEquals(0, Math.hypot());

// Check that Infinity is returned if any of the arguments is +/-Infinity.
assertEquals("Infinity", String(Math.hypot(NaN, Infinity)));
assertEquals("Infinity", String(Math.hypot(1, -Infinity, 2)));

// Check that NaN is returned if any argument is NaN and none is +/-Infinity/
assertTrue(isNaN(Math.hypot(1, 2, NaN)));
assertTrue(isNaN(Math.hypot(NaN, NaN, 4)));

// Check that +0 is returned if all arguments are +/-0.
assertEquals("Infinity", String(1/Math.hypot(-0)));
assertEquals("Infinity", String(1/Math.hypot(0)));
assertEquals("Infinity", String(1/Math.hypot(-0, -0)));
assertEquals("Infinity", String(1/Math.hypot(-0, 0)));

// Check that we avoid overflows and underflows.
assertEqualsDelta(5E300, Math.hypot(3E300, 4E300), 1E285);
assertEqualsDelta(17E-300, Math.hypot(8E-300, 15E-300), 1E-315);
assertEqualsDelta(19E300, Math.hypot(6E300, 6E300, 17E300), 1E285);

// Check that we sufficiently account for rounding errors when summing up.
// For this, we calculate a simple fractal square that recurses in the
// fourth quarter.
var fractals = [];
var edge_length = Math.E * 1E20;

var fractal_length = edge_length;
while(fractal_length >= 1) {
  fractal_length *= 0.5;
  fractals.push(fractal_length);
  fractals.push(fractal_length);
  fractals.push(fractal_length);
}

fractals.push(fractal_length);
assertEqualsDelta(edge_length, Math.hypot.apply(Math, fractals), 1E-15);
fractals.reverse();
assertEqualsDelta(edge_length, Math.hypot.apply(Math, fractals), 1E-15);
// Also shuffle the array.
var c = 0;
function random_sort(a, b) { c++; return (c & 3) - 1.5; }
fractals.sort(random_sort);
assertEqualsDelta(edge_length, Math.hypot.apply(Math, fractals), 1E-15);
fractals.sort(random_sort);
assertEqualsDelta(edge_length, Math.hypot.apply(Math, fractals), 1E-15);


let numOfValueOfCalls = 0;
let obj = { valueOf() { numOfValueOfCalls++; return 0; } };

Math.hypot(obj, obj);
assertEquals(numOfValueOfCalls, 2)
Math.hypot(obj, obj, obj);
assertEquals(numOfValueOfCalls, 5) // 2 + 3
Math.hypot(obj, obj, obj, obj);
assertEquals(numOfValueOfCalls, 9) // 2 + 3 + 4
