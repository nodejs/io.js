// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --maglev --allow-natives-syntax

function f(x) {
  x++; // Making sure that {x} has a Float64 alternative
  let phi = x ? 0 : x; // Creating a Phi whose inputs are Smi and Float64
  const obj = { "d" : phi }; // Storing the Phi in a Smi field, which will
                             // insert a CheckSmi
  --phi; // Using the Smi as an Int32, which will insert a UnsafeSmiUntag
}

%PrepareFunctionForOptimization(f);
f(1.5);
%OptimizeMaglevOnNextCall(f);
f(1.5);
