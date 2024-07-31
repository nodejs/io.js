// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft --turboshaft-verify-reductions

function f() {
  // By allocating an array and storing into it, we delay the constant folding
  // of `arr[0] * arr[1]` to the backend. The corresponding
  // OverflowCheckedBinopOp should thus reach Turboshaft's
  // MachineOptimizationReducer with 2 constant inputs, leading to a reduction
  // that replaces it with a Tuple.
  let arr = [0, 0];
  arr[0] = 42
  arr[1] = 27;
  return arr[0] * arr[1];
}

%PrepareFunctionForOptimization(f);
print(f());
%OptimizeFunctionOnNextCall(f);
print(f());
