// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function array_length(arr) {
  return arr.length;
}

%PrepareFunctionForOptimization(array_length);
assertEquals(3, array_length([1, 2, 3]));
assertEquals(undefined, array_length(3.45));
%OptimizeFunctionOnNextCall(array_length);
assertEquals(3, array_length([1, 2, 3]));
assertEquals(undefined, array_length(3.45));
assertEquals(undefined, array_length(1));
assertOptimized(array_length);
