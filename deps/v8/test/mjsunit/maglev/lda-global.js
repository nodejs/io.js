// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --no-always-turbofan

var b = 1;
function foo(x) {
  return b + x
}

%PrepareFunctionForOptimization(foo);
assertEquals(2, foo(1));
assertEquals(3, foo(2));
%OptimizeMaglevOnNextCall(foo);
assertEquals(4, foo(3));
assertEquals(5, foo(4));
assertTrue(isMaglevved(foo));

// We should deopt here.
b = 2
assertFalse(isMaglevved(foo))
assertEquals(7, foo(5));
