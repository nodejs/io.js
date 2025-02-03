// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file excercises basic fast API calls and enables fuzzing of this
// functionality.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan
// --always-turbofan is disabled because we rely on particular feedback for
// optimizing to the fastest path.
// Flags: --no-always-turbofan
// The test relies on optimizing/deoptimizing at predictable moments, so
// it's not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0
// Flags: --fast-api-allow-float-in-sim

const fast_c_api = new d8.test.FastCAPI();

// ---------- Test 64-bit integer return values -----------

function reset_counts() {
  return fast_c_api.reset_counts();
}

function fast_call_count() {
  return fast_c_api.fast_call_count();
}

function slow_call_count() {
  return fast_c_api.slow_call_count();
}

function sum_int64_as_number(a, b) {
  return fast_c_api.sum_int64_as_number(a, b);
}

function sum_int64_as_bigint(a, b) {
  return fast_c_api.sum_int64_as_bigint(a, b);
}

function sum_uint64_as_number(a, b) {
  return fast_c_api.sum_uint64_as_number(a, b);
}

function sum_uint64_as_bigint(a, b) {
  return fast_c_api.sum_uint64_as_bigint(a, b);
}

let result;

/* ===================== Return as number =====================  */

// === Sum 64-bit signed integers ===

// Prepare
reset_counts();
// Test slow path throwing errors on invalid parameters
assertThrows(() => sum_int64_as_number(136n, 2467247n));
assertEquals(fast_call_count(), 0);
assertEquals(slow_call_count(), 1);

%PrepareFunctionForOptimization(sum_int64_as_number);
result = sum_int64_as_number(0, 0);
assertEquals(fast_call_count(), 0);
assertEquals(slow_call_count(), 2);
assertEquals(result, 0);

// Test Smi path
%OptimizeFunctionOnNextCall(sum_int64_as_number);
result = sum_int64_as_number(0, 0);
assertEquals(fast_call_count(), 1);
assertEquals(slow_call_count(), 2);
assertEquals(result, 0);

// Test non-Smi path
result = sum_int64_as_number(2 ** 33, 2467247);
assertEquals(fast_call_count(), 2);
assertEquals(slow_call_count(), 2);
assertEquals(result, 2 ** 33 + 2467247);

// Test fast call returning over MAX_SAFE_INTEGER
result = sum_int64_as_number(Number.MAX_SAFE_INTEGER, 1024);
assertEquals(fast_call_count(), 3);
assertEquals(slow_call_count(), 2);
assertFalse(Number.isSafeInteger(result));
assertEquals(result, Number.MAX_SAFE_INTEGER + 1024);

// Test fast call returning safe negative number
result = sum_int64_as_number(10, -24737);
assertEquals(fast_call_count(), 4);
assertEquals(slow_call_count(), 2);
assertEquals(result, 10 - 24737);

// Test fast call returning below MIN_SAFE_INTEGER
result = sum_int64_as_number(-1024, Number.MIN_SAFE_INTEGER);
assertEquals(fast_call_count(), 5);
assertEquals(slow_call_count(), 2);
assertFalse(Number.isSafeInteger(result));
assertEquals(result, -1024 + Number.MIN_SAFE_INTEGER);


// === Sum 64-bit unsigned integers ===

// Prepare
reset_counts();
// Test slow path throwing errors on invalid parameters
assertThrows(() => sum_uint64_as_number(136n, 2467247n));
assertEquals(fast_call_count(), 0);
assertEquals(slow_call_count(), 1);

%PrepareFunctionForOptimization(sum_uint64_as_number);
result = sum_uint64_as_number(0, 0);
assertEquals(fast_call_count(), 0);
assertEquals(slow_call_count(), 2);
assertEquals(result, 0);

// Test Smi path
%OptimizeFunctionOnNextCall(sum_uint64_as_number);
result = sum_uint64_as_number(0, 0);
assertEquals(fast_call_count(), 1);
assertEquals(slow_call_count(), 2);
assertEquals(result, 0);

// Test non-Smi path
result = sum_uint64_as_number(2 ** 33, 2467247);
assertEquals(fast_call_count(), 2);
assertEquals(slow_call_count(), 2);
assertEquals(result, 2 ** 33 + 2467247);

// Test fast call returning over MAX_SAFE_INTEGER
result = sum_uint64_as_number(Number.MAX_SAFE_INTEGER, 1024);
assertEquals(fast_call_count(), 3);
assertEquals(slow_call_count(), 2);
assertFalse(Number.isSafeInteger(result));
assertEquals(result, Number.MAX_SAFE_INTEGER + 1024);

/* ===================== Return as BigInt =====================  */


// === Sum 64-bit signed integers ===

// Prepare
reset_counts();
// Test slow path throwing errors on invalid parameters
assertThrows(() => sum_int64_as_bigint(136, 2467247));
assertEquals(fast_call_count(), 0);
assertEquals(slow_call_count(), 1);

%PrepareFunctionForOptimization(sum_int64_as_bigint);
result = sum_int64_as_bigint(0n, 0n);
assertEquals(fast_call_count(), 0);
assertEquals(slow_call_count(), 2);
assertEquals(result, 0n);

// Test Smi path
%OptimizeFunctionOnNextCall(sum_int64_as_bigint);
result = sum_int64_as_bigint(0n, 0n);
assertEquals(fast_call_count(), 1);
assertEquals(slow_call_count(), 2);
assertEquals(result, 0n);

// Test non-Smi path
result = sum_int64_as_bigint(2n ** 33n, 2467247n);
assertEquals(fast_call_count(), 2);
assertEquals(slow_call_count(), 2);
assertEquals(result, 2n ** 33n + 2467247n);

// Test fast call returning over MAX_SAFE_INTEGER
result = sum_int64_as_bigint(BigInt(Number.MAX_SAFE_INTEGER), 1024n);
assertEquals(fast_call_count(), 3);
assertEquals(slow_call_count(), 2);
assertEquals(result, BigInt(Number.MAX_SAFE_INTEGER) + 1024n);

// Test fast call returning safe negative number
result = sum_int64_as_bigint(10n, -24737n);
assertEquals(fast_call_count(), 4);
assertEquals(slow_call_count(), 2);
assertEquals(result, 10n - 24737n);

// Test fast call returning below MIN_SAFE_INTEGER
result = sum_int64_as_bigint(-1024n, BigInt(Number.MIN_SAFE_INTEGER));
assertEquals(fast_call_count(), 5);
assertEquals(slow_call_count(), 2);
assertEquals(result, -1024n + BigInt(Number.MIN_SAFE_INTEGER));


// === Sum 64-bit unsigned integers ===

// Prepare
reset_counts();
// Test slow path throwing errors on invalid parameters
assertThrows(() => sum_uint64_as_bigint(136, 2467247));
assertEquals(fast_call_count(), 0);
assertEquals(slow_call_count(), 1);

%PrepareFunctionForOptimization(sum_uint64_as_bigint);
result = sum_uint64_as_bigint(0n, 0n);
assertEquals(fast_call_count(), 0);
assertEquals(slow_call_count(), 2);
assertEquals(result, 0n);

// Test Smi path
%OptimizeFunctionOnNextCall(sum_uint64_as_bigint);
result = sum_uint64_as_bigint(0n, 0n);
assertEquals(fast_call_count(), 1);
assertEquals(slow_call_count(), 2);
assertEquals(result, 0n);

// Test non-Smi path
result = sum_uint64_as_bigint(2n ** 33n, 2467247n);
assertEquals(fast_call_count(), 2);
assertEquals(slow_call_count(), 2);
assertEquals(result, 2n ** 33n + 2467247n);

// Test fast call returning over MAX_SAFE_INTEGER
result = sum_uint64_as_bigint(BigInt(Number.MAX_SAFE_INTEGER), 1024n);
assertEquals(fast_call_count(), 3);
assertEquals(slow_call_count(), 2);
assertEquals(result, BigInt(Number.MAX_SAFE_INTEGER) + 1024n);
