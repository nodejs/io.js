// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --no-always-turbofan

var b = 1;
function foo() {
  return typeof(b)
}

%PrepareFunctionForOptimization(foo);
assertEquals("number", foo());
assertEquals("number", foo());
%OptimizeMaglevOnNextCall(foo);
assertEquals("number", foo());
assertEquals("number", foo());
assertTrue(isMaglevved(foo));

// We should deopt here.
b = 2
assertFalse(isMaglevved(foo))
assertEquals("number", foo());
