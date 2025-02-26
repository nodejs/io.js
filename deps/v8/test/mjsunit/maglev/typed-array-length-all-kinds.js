// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --js-staging
// Flags: --typed-array-length-loading

function test_int8(size) {
  let a = new Int8Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(test_int8);

function test_uint8(size) {
  let a = new Uint8Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(test_uint8);

function test_uint8clamped(size) {
  let a = new Uint8ClampedArray(size);
  return a.length;
}
%PrepareFunctionForOptimization(test_uint8clamped);

function test_int16(size) {
  let a = new Int16Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(test_int16);

function test_uint16(size) {
  let a = new Uint16Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(test_uint16);

function test_int32(size) {
  let a = new Int32Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(test_int32);

function test_uint32(size) {
  let a = new Uint32Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(test_uint32);

function test_float16(size) {
  let a = new Float16Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(test_float16);

function test_float32(size) {
  let a = new Float32Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(test_float32);

function test_bigint64(size) {
  let a = new BigInt64Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(test_bigint64);

function test_biguint64(size) {
  let a = new BigUint64Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(test_biguint64);

function test(f, size) {
  assertEquals(size, f(size));

  %OptimizeMaglevOnNextCall(f);
  assertEquals(size, f(size));
  assertTrue(isMaglevved(f));
}

test(test_int8, 100);
test(test_uint8, 100);
test(test_uint8clamped, 100);

test(test_int16, 100);
test(test_uint16, 100);

test(test_int32, 100);
test(test_uint32, 100);

test(test_float16, 100);
test(test_float32, 100);

test(test_bigint64, 100);
test(test_biguint64, 100);
