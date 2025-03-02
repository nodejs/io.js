// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --script-context-mutable-heap-number

let y = 42;
y = 4;
(function() {
  function bar() {}
  function foo(a) {
    y = a;
    bar();
    return y + 2;
  }
  assertEquals(7.2, foo(5.2));
})();
