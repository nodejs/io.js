// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-lazy-feedback-allocation

function A() {
  Object.defineProperty(this, 'x', {
    writable: true,
    enumerable: false, // False!
    configurable: true,
    value: undefined
  });
}

class B extends A {
    x = {};
}

%PrepareFunctionForOptimization(B);
let b = new B();
assertEquals(true, b.propertyIsEnumerable("x"));
%OptimizeFunctionOnNextCall(B);
b = new B();
assertEquals(true, b.propertyIsEnumerable("x"));
