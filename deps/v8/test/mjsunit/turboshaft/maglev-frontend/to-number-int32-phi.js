// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function check_int32_is_smi(a, b) {
  let v1 = a + 1_000_000_000;
  let phi = b ? v1 : 42;  // Int32 inputs for the Smi, which means that it can
                          // be untagged to Int32 if it has Int32 uses.
  let v2 = +phi;  // This produces a ToNumber in the bytecode, for which Maglev
                  // inserts a CheckSmi (because of the Smi feedback). If its
                  // input is an untagged Int32 phi, this will become a
                  // CheckInt32IsSmi.
  return phi + v2;  // Int32 use for the Phi so that it's untagged to Int32.
}

%PrepareFunctionForOptimization(check_int32_is_smi);
assertEquals(84, check_int32_is_smi(1, false));
%OptimizeFunctionOnNextCall(check_int32_is_smi);
assertEquals(84, check_int32_is_smi(1, false));
assertOptimized(check_int32_is_smi);

// Triggering a deopt by having making {phi} not fit in Smi range
assertEquals(4_000_000_000, check_int32_is_smi(1_000_000_000, true));
assertUnoptimized(check_int32_is_smi);
