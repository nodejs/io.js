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

const __v_13 = new d8.test.FastCAPI();
  (function () {
    let __v_30 = new Int32Array([-1, 2147483648]);
    function __f_16() {
      return __v_13.add_all_int32_typed_array(__v_30);
    }
      %PrepareFunctionForOptimization(__f_16);
    let __v_32 = __f_16();
      %OptimizeFunctionOnNextCall(__f_16);
      __v_32 = __f_16();
  })();
