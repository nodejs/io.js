// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests exercise WebIDL annotations support in the fast API.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan
// --always-turbofan is disabled because we rely on particular feedback for
// optimizing to the fastest path.
// Flags: --no-always-turbofan
// The test relies on optimizing/deoptimizing at predictable moments, so
// it's not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0

const fast_c_api = new d8.test.FastCAPI();

// ----------- add_all_annotate_enforce_range -----------
// `add_all_annotate_enforce_range` has the following signature:
// double add_all_annotate_enforce_range(bool /*should_fallback*/,
//   int32_t, uint32_t,
//   int64_t, uint64_t)

const limits_params = [
  -(2 ** 31),  // i32
  2 ** 32 - 1,   // u32
  Number.MIN_SAFE_INTEGER,  // i64
  Number.MAX_SAFE_INTEGER   // u64
];
const limits_result = limits_params[0] + limits_params[1] + limits_params[2] + limits_params[3];

function add_all_annotate_enforce_range(params, should_fallback = false) {
  return fast_c_api.add_all_annotate_enforce_range(should_fallback,
    params[0], params[1], params[2], params[3]);
}

%PrepareFunctionForOptimization(add_all_annotate_enforce_range);
assertEquals(limits_result, add_all_annotate_enforce_range(limits_params));
%OptimizeFunctionOnNextCall(add_all_annotate_enforce_range);
assertEquals(limits_result, add_all_annotate_enforce_range(limits_params));

// ----------- enforce_range_compare -----------
// `enforce_range_compare` has the following signature:
// bool enforce_range_compare(bool /*in_range*/,
//   double, integer_type)
// where integer_type = {int32_t, uint32_t, int64_t, uint64_t}

// ----------- i32 -----------
function compare_i32(in_range, arg) {
  return fast_c_api.enforce_range_compare_i32(in_range, arg, arg);
}

%PrepareFunctionForOptimization(compare_i32);
assertFalse(compare_i32(true, 123));
%OptimizeFunctionOnNextCall(compare_i32);
assertTrue(compare_i32(true, 123));
assertTrue(compare_i32(true, -0.5));
assertTrue(compare_i32(true, 0.5));
assertTrue(compare_i32(true, 1.5));
assertTrue(compare_i32(true, -(2 ** 31)));
assertTrue(compare_i32(true, 2 ** 31 - 1));
assertThrows(() => compare_i32(false, -(2 ** 32)));
assertThrows(() => compare_i32(false, -(2 ** 32 + 1)));
assertThrows(() => compare_i32(false, 2 ** 32));
assertThrows(() => compare_i32(false, 2 ** 32 + 3.15));
assertThrows(() => compare_i32(false, Number.MIN_SAFE_INTEGER));
assertThrows(() => compare_i32(false, Number.MAX_SAFE_INTEGER));

// ----------- u32 -----------
function compare_u32(in_range, arg) {
  return fast_c_api.enforce_range_compare_u32(in_range, arg, arg);
}

%PrepareFunctionForOptimization(compare_u32);
assertFalse(compare_u32(true, 123));
%OptimizeFunctionOnNextCall(compare_u32);
assertTrue(compare_u32(true, 123));
assertTrue(compare_u32(true, 0));
assertTrue(compare_u32(true, -0.5));
assertTrue(compare_u32(true, 0.5));
assertTrue(compare_u32(true, 2 ** 32 - 1));
assertThrows(() => compare_u32(false, -(2 ** 31)));
assertThrows(() => compare_u32(false, 2 ** 32));
assertThrows(() => compare_u32(false, -1));
assertThrows(() => compare_u32(false, -1.5));
assertThrows(() => compare_u32(false, Number.MIN_SAFE_INTEGER));
assertThrows(() => compare_u32(false, Number.MAX_SAFE_INTEGER));

// ----------- i64 -----------
function compare_i64(in_range, arg) {
  return fast_c_api.enforce_range_compare_i64(in_range, arg, arg);
}

%PrepareFunctionForOptimization(compare_i64);
assertFalse(compare_i64(true, 123));
%OptimizeFunctionOnNextCall(compare_i64);
assertTrue(compare_i64(true, 123));
assertTrue(compare_i64(true, -0.5));
assertTrue(compare_i64(true, 0.5));
assertTrue(compare_i64(true, 1.5));
assertTrue(compare_i64(true, -(2 ** 63)));
assertTrue(compare_i64(true, Number.MIN_SAFE_INTEGER));
assertTrue(compare_i64(true, Number.MAX_SAFE_INTEGER));
assertThrows(() => compare_i64(false, -(2 ** 64)));
assertThrows(() => compare_i64(false, -(2 ** 64 + 1)));
assertThrows(() => compare_i64(false, 2 ** 64));
assertThrows(() => compare_i64(false, 2 ** 64 + 3.15));

// ----------- u64 -----------
function compare_u64(in_range, arg) {
  return fast_c_api.enforce_range_compare_u64(in_range, arg, arg);
}

%PrepareFunctionForOptimization(compare_u64);
assertFalse(compare_u64(true, 123));
%OptimizeFunctionOnNextCall(compare_u64);
assertTrue(compare_u64(true, 123));
assertTrue(compare_u64(true, 0));
assertTrue(compare_u64(true, -0.5));
assertTrue(compare_u64(true, 0.5));
assertTrue(compare_u64(true, 2 ** 32 - 1));
assertTrue(compare_u64(true, Number.MAX_SAFE_INTEGER));
assertThrows(() => compare_u64(false, 2 ** 64));
assertThrows(() => compare_u64(false, -1));
assertThrows(() => compare_u64(false, -1.5));
assertThrows(() => compare_u64(false, Number.MIN_SAFE_INTEGER));
assertThrows(() => compare_u64(false, 2 ** 64 + 3.15));
