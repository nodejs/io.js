// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function foo(x) {

  function bar(x, y) {
    return [...x, y]
  }

  if (x) {
    return bar`123${x}`
  } else {
    return 42;
  }
}

%PrepareFunctionForOptimization(foo);
assertEquals(42, foo(false));
assertEquals(42, foo(false));
%OptimizeFunctionOnNextCall(foo);
assertEquals(["123", "", 1], foo(1));
assertEquals(["123", "", 2], foo(2));
