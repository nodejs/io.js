// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

function math_smi(x, y, z) {
  let a = x * y;
  a = a + 152;
  a = a / x;
  a++;
  a = a - y;
  a = a % 5;
  a--;
  a = -z + a;
  return a;
}
%PrepareFunctionForOptimization(math_smi);
assertEquals(8, math_smi(4, 3, -5));
assertEquals(8, math_smi(4, 3, -5));
%OptimizeFunctionOnNextCall(math_smi);
assertEquals(8, math_smi(4, 3, -5));
assertOptimized(math_smi);
assertEquals(NaN, math_smi("a", "b"));
assertUnoptimized(math_smi);
