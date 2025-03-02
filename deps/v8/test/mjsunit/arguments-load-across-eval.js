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

// Tests loading of arguments across eval calls.

// Test loading across an eval call that does not shadow variables.
function testNoShadowing(x, h) {
  function f() {
    eval('1');
    assertEquals(1, x);
    assertEquals(2, h());
    function g() {
      assertEquals(1, x);
      assertEquals(2, h());
    }
    g();
  }
  f();
}

testNoShadowing(1, function() { return 2; });

// Test loading across eval calls that do not shadow variables.
function testNoShadowing2(x, h) {
  eval('1');
  function f() {
    eval('1');
    assertEquals(1, x);
    assertEquals(2, h());
    function g() {
      assertEquals(1, x);
      assertEquals(2, h());
    }
    g();
  }
  f();
}

testNoShadowing2(1, function() { return 2; });

// Test loading across an eval call that shadows variables.
function testShadowing(x, h) {
  function f() {
    assertEquals(1, x);
    assertEquals(2, h());
    eval('var x = 3; var h = function() { return 4; };');
    assertEquals(3, x);
    assertEquals(4, h());
    function g() {
      assertEquals(3, x);
      assertEquals(4, h());
    }
    g();
  }
  f();
  assertEquals(1, x);
  assertEquals(2, h());
}

testShadowing(1, function() { return 2; });
