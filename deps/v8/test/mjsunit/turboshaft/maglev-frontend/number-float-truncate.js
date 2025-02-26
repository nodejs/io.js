// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function truncate_number_to_int32(d, b) {
  let v1 = d ^ 42;  // Checked NumberOrOddball truncation
  let p1 = b ? 4.26 : undefined;
  let v2 = p1 | 11;  // (unchecked) NumberOrOddball truncation
  let p2 = b ? 3.35 : 4.59;
  let v3 = p2 & 255;  // (unchecked) Float64 truncation
  return v1 + v2 + v3;
}

%PrepareFunctionForOptimization(truncate_number_to_int32);
assertEquals(61, truncate_number_to_int32(1.253, 1));
assertEquals(58, truncate_number_to_int32(1.253, 0));
%OptimizeFunctionOnNextCall(truncate_number_to_int32);
assertEquals(61, truncate_number_to_int32(1.253, 1));
assertEquals(58, truncate_number_to_int32(1.253, 0));
assertOptimized(truncate_number_to_int32);
