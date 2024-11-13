// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan
// --always-turbofan is disabled because we rely on particular feedback for
// optimizing to the fastest path.
// Flags: --no-always-turbofan
// The test relies on optimizing/deoptimizing at predictable moments, so
// it's not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0
// Flags: --fast-api-allow-float-in-sim

const __v_0 = new d8.test.FastCAPI();

function __f_0(__v_4, __v_5) {
  try {
    // Call the API function with an invalid parameter. Because of the invalid
    // parameter the overload resolution of the fast API cannot figure out
    // which function should be called, and therefore emits code for a normal
    // API call.
    __v_0.add_all_invalid_overload(__v_5, Object.prototype);
  } catch (e) {}
}

%PrepareFunctionForOptimization(__f_0);
__f_0(Number.MIN_VALUE, Number.MIN_VALUE);
%OptimizeFunctionOnNextCall(__f_0);
__f_0(Number.MIN_VALUE, Number.MIN_VALUE);
