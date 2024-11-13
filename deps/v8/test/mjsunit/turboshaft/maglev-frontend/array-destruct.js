// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function destruct_arr() {
  [a, b] = [4, 9];
  return a + b;
}

%PrepareFunctionForOptimization(destruct_arr);
assertEquals(13, destruct_arr());
%OptimizeFunctionOnNextCall(destruct_arr);
assertEquals(13, destruct_arr());
assertOptimized(destruct_arr);
