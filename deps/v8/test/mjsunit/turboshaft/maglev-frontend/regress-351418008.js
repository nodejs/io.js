// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --allow-natives-syntax --no-lazy-feedback-allocation
// Flags: --turbolev --stress-flush-code

let callGC = function () {
  gc();
};

function f() {
  const o = {};
  for (let i = 0; i <= 1; ++i) {
    callGC();
    o.my_property = 2;  // Polymorphic store.
  }
}
%PrepareFunctionForOptimization(f);
f();
%OptimizeFunctionOnNextCall(f);
f();
