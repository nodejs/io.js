// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


(function TestSliced() {
  // toString on the function returns a sliced string on the script source,
  // which is 2-byte.
  const fooString = foo.toString();
  assertTrue(fooString.isWellFormed());
  assertEquals(fooString, fooString.toWellFormed());
})();

function TestCons(a, b) {
  const s = a + b;
  // Flatten it before calling isWellFormed to get a flat cons.
  s.endsWith('a');
  assertTrue(s.isWellFormed());
  assertEquals(s, s.toWellFormed());
}
TestCons('�', '�');

function TestThin(a, b) {
  const s = a + b;
  const o = {};
  o[s];
  assertTrue(s.isWellFormed());
  assertEquals(s, s.toWellFormed());
}
TestThin('�', '�');

function foo() {}
// Make this source file 2-byte.
'�';
