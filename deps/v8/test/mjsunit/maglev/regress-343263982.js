// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

const v2 = [0.1];
function f3() {
  const v8 = [v2];
  for (const v9 in v8) {
    const v10 = v8[v9];
    const v17 = Object.getPrototypeOf({});
    v10[0] = 0;
    v17.e = v17;
  }
}
f3();
f3();
function f20() {
  const v1 = [0.1];
  return v1.entries().next();
}
%PrepareFunctionForOptimization(f20);
%PrepareFunctionForOptimization(f3);
f20();
f20();
%OptimizeMaglevOnNextCall(f20);
f20();
