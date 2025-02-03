// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

// Testing ToBoolean and LogicalNot on various datatypes.

function f(x, y) {
  let a = x * 2;
  let b = a * 2.27;
  return [!a, !y, !b];
}

%PrepareFunctionForOptimization(f);
assertEquals([false, false, false], f(4, 3));
assertEquals([true, true, true], f(0, 0));
%OptimizeFunctionOnNextCall(f);
assertEquals([false, false, false], f(4, 3));
assertEquals([true, true, true], f(0, 0));
assertOptimized(f);
