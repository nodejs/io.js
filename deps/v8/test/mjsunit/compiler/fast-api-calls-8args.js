// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file tests fast callbacks with more than 8 arguments. It should
// fail on arm64 + OSX configuration, because of stack alignment issue,
// see crbug.com/v8/13171.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan
// --always-turbofan is disabled because we rely on particular feedback for
// optimizing to the fastest path.
// Flags: --no-always-turbofan
// The test relies on optimizing/deoptimizing at predictable moments, so
// it's not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0
// Flags: --fast-api-allow-float-in-sim

const add_all_32bit_int_arg1 = -42;
const add_all_32bit_int_arg2 = 45;
const add_all_32bit_int_arg3 = -12345678;
const add_all_32bit_int_arg4 = 0x1fffffff;
const add_all_32bit_int_arg5 = 1e6;
const add_all_32bit_int_arg6 = 1e8;
const add_all_32bit_int_arg7 = 31;
const add_all_32bit_int_arg8 = 63;
const add_all_32bit_int_result_8args = add_all_32bit_int_arg1 +
add_all_32bit_int_arg2 + add_all_32bit_int_arg3 + add_all_32bit_int_arg4 +
add_all_32bit_int_arg5 + add_all_32bit_int_arg6 + add_all_32bit_int_arg7 + add_all_32bit_int_arg8;

const fast_c_api = new d8.test.FastCAPI();

(function () {
  function overloaded_add_all() {
    return fast_c_api.overloaded_add_all_8args(
        add_all_32bit_int_arg1, add_all_32bit_int_arg2, add_all_32bit_int_arg3,
        add_all_32bit_int_arg4, add_all_32bit_int_arg5, add_all_32bit_int_arg6,
        add_all_32bit_int_arg7, add_all_32bit_int_arg8);
  }

  %PrepareFunctionForOptimization(overloaded_add_all);
  let result = overloaded_add_all();
  assertEquals(add_all_32bit_int_result_8args, result);

  fast_c_api.reset_counts();
  %OptimizeFunctionOnNextCall(overloaded_add_all);
  result = overloaded_add_all();
  assertOptimized(overloaded_add_all);

  assertEquals(1, fast_c_api.fast_call_count());
  assertEquals(0, fast_c_api.slow_call_count());
})();
