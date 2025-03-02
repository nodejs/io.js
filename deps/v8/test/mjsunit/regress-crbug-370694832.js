// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertThrows(() => class {
    static {
        class A {
            static {
                [].trigger_error();
            }
            static [eval(super.__proto__)];
        }
    }
});

assertThrows(function f() {
  let x;
  try {
    throw 0;
  }
  catch (x) {
    // This catch is the entry scope

    // Naive use of caches will find the catch-bound x (which is
    // a VAR), and declare 'no conflict'.
    eval("var x;");
  }
});

(function f() {
  let x;
  try {
    throw 0;
  }
  catch (x) {
    // This catch is the entry scope

    // Naive use of caches will find the catch-bound x (which is
    // a VAR), and determine that this function can be hoisted.
    eval("{ function x() {} }");
  }
  assertEquals(undefined, x);
})();

assertEquals(undefined, (function f() {
  with ({}) {
    // This with is the entry scope, any other scope would do
    // though.

    // The conflict check on `var f` caches the function name
    // variable on the function scope, the subsequent 'real'
    // lookup of `f` caches the function name variable on the
    // entry i.e. with scope.
    return eval("var f; f;");
  }
})());
