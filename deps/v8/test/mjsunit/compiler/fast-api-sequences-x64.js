// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file adds x64 specific tests to the ones in fast-api-sequence.js.

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

assertTrue(fast_c_api.supports_fp_params);

(function () {
  const max_safe_float = 2 ** 24 - 1;
  const add_all_result = -42 + 45 +
    Number.MIN_SAFE_INTEGER + Number.MAX_SAFE_INTEGER +
    max_safe_float * 0.5 + Math.PI;

  function add_all_sequence() {
    const arr = [-42, 45,
    Number.MIN_SAFE_INTEGER, Number.MAX_SAFE_INTEGER,
    max_safe_float * 0.5, Math.PI];
    return fast_c_api.add_all_sequence(arr);
  }
  ExpectFastCall(add_all_sequence, add_all_result);
})();

const max_safe_as_bigint = BigInt(Number.MAX_SAFE_INTEGER);
(function () {
  function int64_test() {
    let typed_array = new BigInt64Array([-42n, 1n, max_safe_as_bigint]);
    return fast_c_api.add_all_int64_typed_array(
      typed_array);
  }
  const expected = Number(BigInt.asIntN(64, -42n + 1n + max_safe_as_bigint));
  ExpectFastCall(int64_test, expected);
})();

(function () {
  function uint64_test() {
    let typed_array = new BigUint64Array([max_safe_as_bigint, 1n, 2n]);
    return fast_c_api.add_all_uint64_typed_array(
      typed_array);
  }
  const expected = Number(BigInt.asUintN(64, max_safe_as_bigint + 1n + 2n));
  ExpectFastCall(uint64_test, expected);
})();
