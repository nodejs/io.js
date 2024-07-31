// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function f0() {
  const v0 = [];
  v0.d = undefined;
  function f1(v3) {
    v3.d = v3;
  }
  %PrepareFunctionForOptimization(f1);
  [0, v0].reduceRight(f1);
}
%PrepareFunctionForOptimization(f0);
f0();
%OptimizeFunctionOnNextCall(f0);
f0();
