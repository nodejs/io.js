// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-assert-types

const very_close_to_zero = 0.0000000000000000000001;
const huge_number = 10**307;

function test(b) {
  // We perform an arithmetic operation such that the typer can eliminate
  // the -0 on a.
  let a = b - very_close_to_zero;
  if(a < 0) return a / huge_number;
  return 42;
}

%PrepareFunctionForOptimization(test);
test(-50);
%OptimizeFunctionOnNextCall(test);
assertEquals(-0, test(-very_close_to_zero));
