// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

const v0 = [-988625.4670131855];
v0[2] = v0;

const v1 = [-30777,-49785];

for (let v2 = 0; v2 < 5; v2++) {
  function f3() {
  }
  function f7(a8) {
    a8[1];
    a8.f = a8;
    a8.forEach(f3);
  }

  %PrepareFunctionForOptimization(f7);
  f7(v0);
  f7(v1);

  %OptimizeFunctionOnNextCall(f7);
}
