// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function I31RefBasic() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  builder.addFunction("signed", kSig_i_i)
    .addLocals(wasmRefType(kWasmI31Ref), 1)
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprRefI31, kExprLocalTee, 1,
              kGCPrefix, kExprI31GetS])
    .exportFunc();
  builder.addFunction("unsigned", kSig_i_i)
    .addLocals(wasmRefType(kWasmI31Ref), 1)
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprRefI31, kExprLocalTee, 1,
              kGCPrefix, kExprI31GetU])
    .exportFunc();

  let instance = builder.instantiate();

  assertEquals(123, instance.exports.signed(123));
  assertEquals(123, instance.exports.unsigned(123));
  // Truncation:
  assertEquals(0x1234, instance.exports.signed(0x80001234));
  assertEquals(0x1234, instance.exports.unsigned(0x80001234));
  // Sign/zero extention:
  assertEquals(-1, instance.exports.signed(0x7fffffff));
  assertEquals(0x7fffffff, instance.exports.unsigned(0x7fffffff));
})();

(function I31RefNullable() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  builder.addFunction("i31_null", kSig_i_i)
    .addLocals(wasmRefNullType(kWasmI31Ref), 1)
    .addBody([
        kExprLocalGet, 0,
        kExprIf, kWasmVoid,
          kExprRefNull, kI31RefCode, kExprLocalSet, 1,
        kExprElse,
          ...wasmI32Const(42), kGCPrefix, kExprRefI31, kExprLocalSet, 1,
        kExprEnd,
        kExprLocalGet, 1, kGCPrefix, kExprI31GetS])
    .exportFunc();

  let instance = builder.instantiate();

  assertEquals(42, instance.exports.i31_null(0));
  assertTraps(kTrapNullDereference, () => instance.exports.i31_null(1));
})();

(function I31RefJS() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  builder.addFunction("roundtrip", makeSig([kWasmExternRef], [kWasmExternRef]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprAnyConvertExtern,
              kGCPrefix, kExprExternConvertAny])
    .exportFunc();
  builder.addFunction("signed", makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprAnyConvertExtern,
              kGCPrefix, kExprRefCast, kI31RefCode, kGCPrefix, kExprI31GetS])
    .exportFunc();
  builder.addFunction("unsigned", makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprAnyConvertExtern,
              kGCPrefix, kExprRefCast, kI31RefCode, kGCPrefix, kExprI31GetU])
    .exportFunc();
  builder.addFunction("new", makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprRefI31,
              kGCPrefix, kExprExternConvertAny])
    .exportFunc();

  let instance = builder.instantiate();

  assertEquals(0, instance.exports.roundtrip(0));
  assertEquals(0, instance.exports.signed(0));
  assertEquals(0, instance.exports.unsigned(0));
  assertEquals(0, instance.exports.new(0));

  assertEquals(123, instance.exports.roundtrip(123));
  assertEquals(123, instance.exports.signed(123));
  assertEquals(123, instance.exports.unsigned(123));
  assertEquals(123, instance.exports.new(123));

  // Max value.
  assertEquals(0x3fffffff, instance.exports.roundtrip(0x3fffffff));
  assertEquals(0x3fffffff, instance.exports.signed(0x3fffffff));
  assertEquals(0x3fffffff, instance.exports.unsigned(0x3fffffff));
  assertEquals(0x3fffffff, instance.exports.new(0x3fffffff));

  // Double number.
  assertEquals(1234.567, instance.exports.roundtrip(1234.567));
  assertTraps(kTrapIllegalCast, () => instance.exports.signed(1234.567));
  assertTraps(kTrapIllegalCast, () => instance.exports.unsigned(1234.567));

  // Out-of-bounds positive integer.
  assertEquals(0x40000000, instance.exports.roundtrip(0x40000000));
  assertTraps(kTrapIllegalCast, () => instance.exports.signed(0x40000000));
  assertTraps(kTrapIllegalCast, () => instance.exports.unsigned(0x40000000));
  assertEquals(-0x40000000, instance.exports.new(0x40000000));

  // Out-of-bounds negative integer.
  assertEquals(-0x40000001, instance.exports.roundtrip(-0x40000001));
  assertTraps(kTrapIllegalCast, () => instance.exports.signed(-0x40000001));
  assertTraps(kTrapIllegalCast, () => instance.exports.unsigned(-0x40000001));
  assertEquals(0x3fffffff, instance.exports.new(-0x40000001));

  // Sign/zero extention.
  assertEquals(-2, instance.exports.roundtrip(-2));
  assertEquals(-2, instance.exports.signed(-2));
  assertEquals(0x7ffffffe, instance.exports.unsigned(-2));
  assertEquals(-2, instance.exports.new(-2));

  // Min value.
  assertEquals(-0x40000000, instance.exports.roundtrip(-0x40000000));
  assertEquals(-0x40000000, instance.exports.signed(-0x40000000));
  assertEquals(0x40000000, instance.exports.unsigned(-0x40000000));
  assertEquals(-0x40000000, instance.exports.new(-0x40000000));

  assertEquals(NaN, instance.exports.roundtrip(NaN));
  assertTraps(kTrapIllegalCast, () => instance.exports.signed(NaN));
  assertTraps(kTrapIllegalCast, () => instance.exports.unsigned(NaN));

  assertEquals(-0, instance.exports.roundtrip(-0));
  assertTraps(kTrapIllegalCast, () => instance.exports.signed(-0));
  assertTraps(kTrapIllegalCast, () => instance.exports.unsigned(-0));

  assertEquals(Infinity, instance.exports.roundtrip(Infinity));
  assertTraps(kTrapIllegalCast, () => instance.exports.signed(Infinity));
  assertTraps(kTrapIllegalCast, () => instance.exports.unsigned(Infinity));

  assertEquals(-Infinity, instance.exports.roundtrip(-Infinity));
  assertTraps(kTrapIllegalCast, () => instance.exports.signed(-Infinity));
  assertTraps(kTrapIllegalCast, () => instance.exports.unsigned(-Infinity));
})();
