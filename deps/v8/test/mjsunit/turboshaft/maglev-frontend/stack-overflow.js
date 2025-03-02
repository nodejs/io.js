// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function stack_overflow() {
  try {
    return stack_overflow();
  } catch(e) {
    return 42;
  }
}

%PrepareFunctionForOptimization(stack_overflow);
assertEquals(42, stack_overflow());

%OptimizeFunctionOnNextCall(stack_overflow);
assertEquals(42, stack_overflow());
assertOptimized(stack_overflow);
