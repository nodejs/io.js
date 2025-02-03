// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f(x, y, z) {
  return x + y + z;
}
%NeverOptimizeFunction(f);

function f_forward_args() {
  return f.apply(null, arguments);
}

%PrepareFunctionForOptimization(f_forward_args);
assertEquals(24, f_forward_args(12, 5, 7));
assertEquals(24, f_forward_args(12, 5, 7, 19));
%OptimizeFunctionOnNextCall(f_forward_args);
assertEquals(24, f_forward_args(12, 5, 7));
assertEquals(24, f_forward_args(12, 5, 7, 19));
assertOptimized(f_forward_args);
