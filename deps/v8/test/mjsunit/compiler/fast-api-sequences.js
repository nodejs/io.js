// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file excercises sequences support for  fast API calls.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan
// --always-turbofan is disabled because we rely on particular feedback for
// optimizing to the fastest path.
// Flags: --no-always-turbofan
// The test relies on optimizing/deoptimizing at predictable moments, so
// it's not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0
// Flags: --fast-api-allow-float-in-sim

d8.file.execute('test/mjsunit/compiler/fast-api-helpers.js');

const fast_c_api = new d8.test.FastCAPI();

// ----------- add_all_sequence -----------
// `add_all_sequence` has the following signature:
// double add_all_sequence(Local<Array>)

// Smi only test - regular call hits the fast path.
(function () {
  function add_all_sequence() {
    const arr = [-42, 45];
    return fast_c_api.add_all_sequence(arr);
  }
  ExpectFastCall(add_all_sequence, 3);
})();

(function () {
  function add_all_sequence_mismatch(arg) {
    return fast_c_api.add_all_sequence(arg);
  }

  %PrepareFunctionForOptimization(add_all_sequence_mismatch);
  add_all_sequence_mismatch();
  %OptimizeFunctionOnNextCall(add_all_sequence_mismatch);

  // Test that passing non-array arguments falls down the slow path.
  assert_throws_and_optimized(add_all_sequence_mismatch, {});
  assert_throws_and_optimized(add_all_sequence_mismatch, 'string');
  assert_throws_and_optimized(add_all_sequence_mismatch, Symbol());
})();

//----------- Test function overloads with same arity. -----------
//Only overloads between JSArray and TypedArray are supported

// Test with TypedArray.
(function () {
  function overloaded_test() {
    let typed_array = new Uint32Array([1,2,3]);
    return fast_c_api.add_all_overload(typed_array);
  }
  ExpectFastCall(overloaded_test, 6);
})();

let large_array = [];
for (let i = 0; i < 100; i++) {
  large_array.push(i);
}

// Non-externalized TypedArray.
(function () {
  function overloaded_test() {
    let typed_array = new Uint32Array(large_array);
    return fast_c_api.add_all_overload(
      typed_array);
  }
  ExpectFastCall(overloaded_test, 4950);
})();

// Test with JSArray.
(function () {
  function overloaded_test() {
    let js_array = [26, -6, 42];
    return fast_c_api.add_all_overload(js_array);
  }
  ExpectFastCall(overloaded_test, 62);
})();

// ----------- Test different TypedArray functions. -----------
// ----------- add_all_<TYPE>_typed_array -----------
// `add_all_<TYPE>_typed_array` have the following signature:
// double add_all_<TYPE>_typed_array(bool /*should_fallback*/, FastApiTypedArray<TYPE>)

(function () {
  function uint8_test() {
    let typed_array = new Uint8Array([1, 2, 3]);
    return fast_c_api.add_all_uint8_typed_array(
      typed_array);
  }
  ExpectFastCall(uint8_test, 6);
})();

(function () {
  function int32_test() {
    let typed_array = new Int32Array([-42, 1, 2, 3]);
    return fast_c_api.add_all_int32_typed_array(
      typed_array);
  }
  ExpectFastCall(int32_test, -36);
})();

(function () {
  function uint32_test() {
    let typed_array = new Uint32Array([1, 2, 3]);
    return fast_c_api.add_all_uint32_typed_array(
      typed_array);
  }
  ExpectFastCall(uint32_test, 6);
})();

(function () {
  function float32_test() {
    let typed_array = new Float32Array([1.25, 2.25, 3.5]);
    return fast_c_api.add_all_float32_typed_array(
      typed_array);
  }
    ExpectFastCall(float32_test, 7);
})();

(function () {
  function float64_test() {
    let typed_array = new Float64Array([1.25, 2.25, 3.5]);
    return fast_c_api.add_all_float64_typed_array(
      typed_array);
  }
    ExpectFastCall(float64_test, 7);
})();

// ----------- TypedArray tests in various conditions -----------
// Detached backing store.
(function () {
  function detached_typed_array_test() {
    let typed_array = new Int32Array([-42, 1, 2, 3]);
    %ArrayBufferDetach(typed_array.buffer);
    return fast_c_api.add_all_int32_typed_array(
      typed_array);
  }
  ExpectFastCall(detached_typed_array_test, 0);
})();

// Detached, non-externalised backing store.
(function () {
  function detached_non_ext_typed_array_test() {
    let typed_array = new Int32Array(large_array);
    %ArrayBufferDetach(typed_array.buffer);
    return fast_c_api.add_all_int32_typed_array(
      typed_array);
  }
  ExpectFastCall(detached_non_ext_typed_array_test, 0);
})();

// Detaching the backing store after the function is optimised.
(function() {
  let typed_array = new Int32Array([-42, 1, 2, 3]);
  let expected = -36;
  function detached_later_typed_array_test() {
    return fast_c_api.add_all_int32_typed_array(
      typed_array);
  }

  %PrepareFunctionForOptimization(detached_later_typed_array_test);
  let result = detached_later_typed_array_test();
  assertEquals(expected, result);

  fast_c_api.reset_counts();
  %OptimizeFunctionOnNextCall(detached_later_typed_array_test);
  result = detached_later_typed_array_test();
  assertEquals(expected, result);
  assertOptimized(detached_later_typed_array_test);
  assertEquals(1, fast_c_api.fast_call_count());
  assertEquals(0, fast_c_api.slow_call_count());
  fast_c_api.reset_counts();

  // Detaching the backing store after the function was optimised should
  // not lead to a deopt.
  %ArrayBufferDetach(typed_array.buffer);
  result = detached_later_typed_array_test();
  assertOptimized(detached_later_typed_array_test);
  assertEquals(1, fast_c_api.fast_call_count());
  assertEquals(0, result);
})();

// Externalised backing store.
(function () {
  function shared_array_buffer_ta_test() {
    let sab = new SharedArrayBuffer(16);
    let typed_array = new Int32Array(sab);
    typed_array.set([-42, 1, 2, 3]);
    return fast_c_api.add_all_int32_typed_array(
      typed_array);
  }
  ExpectFastCall(shared_array_buffer_ta_test, -36);
})();

// Externalised backing store.
(function () {
  function shared_array_buffer_ext_ta_test() {
    let sab = new SharedArrayBuffer(400);
    let typed_array = new Int32Array(sab);
    typed_array.set(large_array);
    return fast_c_api.add_all_int32_typed_array(
      typed_array);
  }
  ExpectFastCall(shared_array_buffer_ext_ta_test, 4950);
})();

// Empty TypedArray.
(function () {
  function int32_test() {
    let typed_array = new Int32Array(0);
    return fast_c_api.add_all_int32_typed_array(
      typed_array);
  }
  ExpectFastCall(int32_test, 0);
})();

(function () {
  function uint8_test() {
    let typed_array = new Uint8Array(0);
    return fast_c_api.add_all_uint8_typed_array(
      typed_array);
  }
  ExpectFastCall(uint8_test, 0);
})();

// Values out of [0, 255] range are properly truncated.
(function() {
  function uint8_test() {
    let typed_array = new Uint8Array([0, 256, -1]);
    return fast_c_api.add_all_uint8_typed_array(
      typed_array);
  }
  ExpectFastCall(uint8_test, 255);
})();

// Invalid argument types instead of a TypedArray.
(function () {
  function invalid_test(arg) {
    return fast_c_api.add_all_int32_typed_array(
      arg);
  }
  %PrepareFunctionForOptimization(invalid_test);
  invalid_test(new Int32Array(0));
  %OptimizeFunctionOnNextCall(invalid_test);

  assert_throws_and_optimized(invalid_test, 42);
  assert_throws_and_optimized(invalid_test, {});
  assert_throws_and_optimized(invalid_test, 'string');
  assert_throws_and_optimized(invalid_test, Symbol());
})();

// Passing `null` instead of a TypedArray in a non-overloaded function.
(function () {
  function null_test(arg) {
    return fast_c_api.add_all_int32_typed_array(
      arg);
  }
  %PrepareFunctionForOptimization(null_test);
  null_test(new Int32Array(0));
  %OptimizeFunctionOnNextCall(null_test);

  assert_throws_and_optimized(null_test, null);
})();

// Passing `null` instead of a TypedArray in a non-overloaded function.
(function () {
  function invalid_value() {
    const arr = new Array(2);
    Object.defineProperty(Array.prototype, 1, {
      get() {
        throw new 'get is called.';
      }
    });

    fast_c_api.add_all_sequence(arr);
  }
  assertThrows(() => invalid_value());
})();
