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

const __v_0 = new d8.test.FastCAPI();
function __f_2(__v_5) {
    __v_0.copy_string(__v_5, __v_5);
}
  %PrepareFunctionForOptimization(__f_2);
  __f_2();
  %OptimizeFunctionOnNextCall(__f_2);
  assertThrows(
      () => __f_2('Hello World'), Error,
      'Invalid parameter, the second parameter has to be a a Uint8Array.');
