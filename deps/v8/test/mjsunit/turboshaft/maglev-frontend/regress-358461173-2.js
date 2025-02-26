// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function g() { ThrowSomething(); }
%NeverOptimizeFunction(g);

function foo() {
  let sum = 0;
  // Loop with medium fixed iteration count --> will be partially unrolled.
  for (let i = 0; i < 147; i++) {
    try {
      g();
      for (;true;) {}
    } catch (e) {
      sum += 1;
    }
  }
  return sum;
}

%PrepareFunctionForOptimization(foo);
assertEquals(147, foo());

%OptimizeFunctionOnNextCall(foo);
assertEquals(147, foo());
