// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function bitwise_smi(a, b) {
  let x = a | b;
  x = x & 52358961;
  x = x ^ b;
  x = x >> 2;
  x = x << 5;
  x = x >>> 1;
  return ~x;
}
%PrepareFunctionForOptimization(bitwise_smi);
assertEquals(-23041, bitwise_smi(1548, 45235));
assertEquals(-23041, bitwise_smi(1548, 45235));
%OptimizeFunctionOnNextCall(bitwise_smi);
assertEquals(-23041, bitwise_smi(1548, 45235));
assertOptimized(bitwise_smi);
