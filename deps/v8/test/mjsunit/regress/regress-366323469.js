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


const __v_7 = new d8.test.FastCAPI();

function __f_3(__v_12) {
  const __v_13 = new Uint8Array();
    __v_7.copy_string(__v_12, __v_13);
}
  %PrepareFunctionForOptimization(__f_3);
  __f_3('Hello');
  %OptimizeFunctionOnNextCall(__f_3);
  assertThrows(() => __f_3('Hello'), Error, 'Invalid parameter, destination array is too small.');
