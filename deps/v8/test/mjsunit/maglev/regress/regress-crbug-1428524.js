// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --maglev --allow-natives-syntax --maglev-untagged-phis

function f(a) {
  let int32 = a + 1;

  let int32_phi = a ? int32 : 42;
  let int32_use = int32_phi + 1;

  // {float64_phi} has an Int32 input and a Float64 use, we should thus generate
  // a Float64 phi for it. One of its inputs is {int32_phi}, which should have
  // been untagged as Int32, and for which we should thus require an
  // Int32ToFloat64 conversion.
  let float64_phi = a ? int32_phi : int32;
  let float64_use = float64_phi + 4.5;

  return float64_use;
}

%PrepareFunctionForOptimization(f);
f(1);
%OptimizeMaglevOnNextCall(f);
f(1);
