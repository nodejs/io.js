// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbofan --no-always-turbofan --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/gc-js-interop-helpers.js');

let {struct, array} = CreateWasmObjects();
for (const wasm_obj of [struct, array]) {

  // Test numeric operators.
  testThrowsRepeated(() => ++wasm_obj, TypeError);
  testThrowsRepeated(() => wasm_obj--, TypeError);
  testThrowsRepeated(() => +wasm_obj, TypeError);
  testThrowsRepeated(() => -wasm_obj, TypeError);
  testThrowsRepeated(() => ~wasm_obj, TypeError);
  testThrowsRepeated(() => wasm_obj - 2, TypeError);
  testThrowsRepeated(() => wasm_obj * 2, TypeError);
  testThrowsRepeated(() => wasm_obj / 2, TypeError);
  testThrowsRepeated(() => wasm_obj ** 2, TypeError);
  testThrowsRepeated(() => wasm_obj << 2, TypeError);
  testThrowsRepeated(() => wasm_obj >> 2, TypeError);
  testThrowsRepeated(() => 2 >>> wasm_obj, TypeError);
  testThrowsRepeated(() => 2 % wasm_obj, TypeError);
  testThrowsRepeated(() => wasm_obj | 1, TypeError);
  testThrowsRepeated(() => 1 & wasm_obj, TypeError);
  testThrowsRepeated(() => wasm_obj ^ wasm_obj, TypeError);
  testThrowsRepeated(() => wasm_obj += 1, TypeError);
  let tmp = 1;
  testThrowsRepeated(() => tmp += wasm_obj, TypeError);
  testThrowsRepeated(() => tmp <<= wasm_obj, TypeError);
  testThrowsRepeated(() => tmp &= wasm_obj, TypeError);
  testThrowsRepeated(() => tmp **= wasm_obj, TypeError);

  // Test numeric functions of the global object.
  testThrowsRepeated(() => isFinite(wasm_obj), TypeError);
  testThrowsRepeated(() => isNaN(wasm_obj), TypeError);
  testThrowsRepeated(() => parseFloat(wasm_obj), TypeError);
  testThrowsRepeated(() => parseInt(wasm_obj), TypeError);

  // Test Number.
  repeated(() => assertFalse(Number.isFinite(wasm_obj)));
  repeated(() => assertFalse(Number.isInteger(wasm_obj)));
  repeated(() => assertFalse(Number.isNaN(wasm_obj)));
  repeated(() => assertFalse(Number.isSafeInteger(wasm_obj)));
  testThrowsRepeated(() => Number.parseFloat(wasm_obj), TypeError);
  testThrowsRepeated(() => Number.parseInt(wasm_obj), TypeError);

  // Test BigInt.
  testThrowsRepeated(() => BigInt.asIntN(2, wasm_obj), TypeError);
  testThrowsRepeated(
      () => BigInt.asUintN(wasm_obj, 123n), TypeError);

  // Test Math.
  testThrowsRepeated(() => Math.abs(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.acos(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.acosh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.asin(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.asinh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.atan(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.atanh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.atan2(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.cbrt(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.ceil(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.clz32(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.cos(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.cosh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.exp(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.expm1(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.floor(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.fround(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.hypot(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.imul(wasm_obj, wasm_obj), TypeError);
  testThrowsRepeated(() => Math.log(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.log1p(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.log10(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.log2(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.max(2, wasm_obj), TypeError);
  testThrowsRepeated(() => Math.min(2, wasm_obj), TypeError);
  testThrowsRepeated(() => Math.pow(2, wasm_obj), TypeError);
  testThrowsRepeated(() => Math.pow(wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Math.round(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.sign(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.sin(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.sinh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.sqrt(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.tan(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.tanh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.trunc(wasm_obj), TypeError);

  // Test boolean.
  repeated(() => assertFalse(!wasm_obj));
  repeated(() => assertTrue(wasm_obj ? true : false));
  tmp = true;
  repeated(() => assertSame(wasm_obj, tmp &&= wasm_obj));
  tmp = 0;
  repeated(() => assertSame(wasm_obj, tmp ||= wasm_obj));
  tmp = null;
  repeated(() => assertSame(wasm_obj, tmp ??= wasm_obj));

  // Ensure no statement re-assigned wasm_obj by accident.
  assertTrue(wasm_obj == struct || wasm_obj == array);
}
