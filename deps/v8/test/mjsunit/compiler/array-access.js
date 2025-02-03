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

function Get0(a) {
  return a[0];
}

function GetN(a,n) {
  return a[n];
}

function GetA0(a) {
  return a[a[0]];
}

function GetAN(a,n) {
  return a[a[n]];
}

function GetAAN(a,n) {
  return a[a[a[n]]];
}

function RunGetTests(packed=true) {
  if (packed) {
    var a = [2,0,1];
    assertEquals(2, Get0(a));

    assertEquals(2, GetN(a, 0));
    assertEquals(0, GetN(a, 1));
    assertEquals(1, GetN(a, 2));

    assertEquals(1, GetA0(a));

    assertEquals(1, GetAN(a,0));
    assertEquals(2, GetAN(a,1));
    assertEquals(0, GetAN(a,2));

    assertEquals(0, GetAAN(a,0));
    assertEquals(1, GetAAN(a,1));
    assertEquals(2, GetAAN(a,2));
  } else {
    var a = ['2','0','1'];
    assertEquals('2', Get0(a));

    assertEquals('2', GetN(a, 0));
    assertEquals('0', GetN(a, 1));
    assertEquals('1', GetN(a, 2));

    assertEquals('1', GetA0(a));

    assertEquals('1', GetAN(a,0));
    assertEquals('2', GetAN(a,1));
    assertEquals('0', GetAN(a,2));

    assertEquals('0', GetAAN(a,0));
    assertEquals('1', GetAAN(a,1));
    assertEquals('2', GetAAN(a,2));
  }
}


function Set07(a) {
  a[0] = 7;
}

function Set0V(a, v) {
  a[0] = v;
}

function SetN7(a, n) {
  a[n] = 7;
}

function SetNX(a, n, x) {
  a[n] = x;
}

function RunSetTests(a, packed=true) {
  Set07(a);
  if (packed) {
    assertEquals(7, a[0]);
  }
  assertEquals(0, a[1]);
  assertEquals(0, a[2]);

  Set0V(a, 1);
  if (packed) {
    assertEquals(1, a[0]);
  }
  assertEquals(0, a[1]);
  assertEquals(0, a[2]);

  SetN7(a, 2);
  if (packed) {
    assertEquals(1, a[0]);
  }
  assertEquals(0, a[1]);
  assertEquals(7, a[2]);

  SetNX(a, 1, 5);
  if (packed) {
    assertEquals(1, a[0]);
  }
  assertEquals(5, a[1]);
  assertEquals(7, a[2]);

  for (var i = 0; i < 3; i++) SetNX(a, i, 0);
  if (packed) {
    assertEquals(0, a[0]);
  }
  assertEquals(0, a[1]);
  assertEquals(0, a[2]);
}

function RunArrayBoundsCheckTest() {
  var g = [1,2,3];

  function f(a, i) { a[i] = 42; }

  for (var i = 0; i < 100000; i++) { f(g, 0); }

  f(g, 4);

  assertEquals(42, g[0]);
  assertEquals(42, g[4]);
}

var a = [0,0,0];
var o = {0: 0, 1: 0, 2: 0};
for (var i = 0; i < 1000; i++) {
  RunGetTests();
  RunSetTests(a);
  RunSetTests(o);
}

RunArrayBoundsCheckTest();

// Packed
// Non-extensible
a = Object.preventExtensions([0,0,0,'a']);
o = Object.preventExtensions({0: 0, 1: 0, 2: 0});
for (var i = 0; i < 1000; i++) {
  RunGetTests();
  RunGetTests(false);
  RunSetTests(a);
  RunSetTests(o);
}

// Sealed
a = Object.seal([0,0,0,'a']);
o = Object.seal({0: 0, 1: 0, 2: 0});
for (var i = 0; i < 1000; i++) {
  RunGetTests();
  RunGetTests(false);
  RunSetTests(a);
  RunSetTests(o);
}

// Frozen
a = Object.freeze([0,0,0,'a']);
o = Object.freeze({0: 0, 1: 0, 2: 0});
for (var i = 0; i < 1000; i++) {
  RunGetTests();
  RunGetTests(false);
}

// Holey
// Non-extensible
a = Object.preventExtensions([,0,0,'a']);
for (var i = 0; i < 1000; i++) {
  RunGetTests();
  RunGetTests(false);
  RunSetTests(a, false);
}

// Sealed
a = Object.seal([,0,0,'a']);
for (var i = 0; i < 1000; i++) {
  RunGetTests();
  RunGetTests(false);
  RunSetTests(a, false);
}

// Frozen
a = Object.freeze([,0,0,'a']);
for (var i = 0; i < 1000; i++) {
  RunGetTests();
  RunGetTests(false);
}
