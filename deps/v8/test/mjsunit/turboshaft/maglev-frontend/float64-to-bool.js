// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function ret_bool(a) {
  let v = a * 1.35;
  return !v;
}

%PrepareFunctionForOptimization(ret_bool);
assertEquals(false, ret_bool(1.5));
assertEquals(true, ret_bool(0));
assertEquals(true, ret_bool(NaN));
%OptimizeFunctionOnNextCall(ret_bool);
assertEquals(false, ret_bool(1.5));
assertEquals(true, ret_bool(0));
assertEquals(true, ret_bool(NaN));
assertOptimized(ret_bool);
