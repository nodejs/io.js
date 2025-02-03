// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function check_number(x) {
  // With only number maps in the feedback, "+x" is a no-op and Maglev just
  // emits a CheckNumber.
  return +x;
}

%PrepareFunctionForOptimization(check_number);
assertEquals(3.65, check_number(3.65));
%OptimizeFunctionOnNextCall(check_number);
assertEquals(3.65, check_number(3.65));
assertOptimized(check_number);
// CheckNumber should trigger a deopt for non-number maps (like strings).
assertEquals(NaN, check_number("abc"));
assertUnoptimized(check_number);
