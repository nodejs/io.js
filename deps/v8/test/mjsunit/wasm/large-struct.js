// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// Keep in sync with wasm-limits.h.
let kWasmMaxStructFields = 10000;
// Keep in sync with wasm-constants.h.
let kMaxStructFieldIndexForImplicitNullCheck = 4000;

(function TestLargeStruct() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct_large = builder.addStruct(
      new Array(kWasmMaxStructFields).fill(makeField(kWasmS128, true)));

  let struct_large_indices = [
    0, kMaxStructFieldIndexForImplicitNullCheck,
    kMaxStructFieldIndexForImplicitNullCheck + 1,
    kWasmMaxStructFields - 1];

  for (let field_index of struct_large_indices) {
    builder.addFunction(
        "structLargeGet" + field_index,
        makeSig([wasmRefNullType(struct_large)], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, kExprStructGet, struct_large,
        ...wasmUnsignedLeb(field_index),
        kSimdPrefix, kExprI32x4ExtractLane, 0])
      .exportFunc();

    builder.addFunction(
        "structLargeSet" + field_index,
        makeSig([wasmRefNullType(struct_large)], []))
      .addBody([
        kExprLocalGet, 0,
        kSimdPrefix, kExprS128Const,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
        kGCPrefix, kExprStructSet, struct_large,
        ...wasmUnsignedLeb(field_index)])
      .exportFunc();
  }

  builder.addFunction("structLargeMake",
                      makeSig([], [wasmRefType(struct_large)]))
      .addBody([kGCPrefix, kExprStructNewDefault, struct_large])
      .exportFunc();

  let instance = builder.instantiate();

  let struct_large_obj = instance.exports.structLargeMake();
  for (let field_index of struct_large_indices) {
    assertTraps(kTrapNullDereference,
      () => instance.exports[
          "structLargeGet" + field_index](null));
    assertTraps(kTrapNullDereference,
      () => instance.exports[
          "structLargeSet" + field_index](null));
    instance.exports["structLargeSet" + field_index](struct_large_obj);
    assertEquals(0x03020100,
                  instance.exports["structLargeGet" + field_index](
                    struct_large_obj));
  }
})();

(function TestLargeStructRef() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let element_type = builder.addStruct([makeField(kWasmI32, true)]);
  let struct_large = builder.addStruct(
      new Array(kWasmMaxStructFields).fill(
          makeField(wasmRefNullType(element_type), true)));

  let struct_large_indices = [
    0, kMaxStructFieldIndexForImplicitNullCheck,
    kMaxStructFieldIndexForImplicitNullCheck + 1,
    kWasmMaxStructFields - 1];

  for (let field_index of struct_large_indices) {
    builder.addFunction(
        "structLargeGet" + field_index,
        makeSig([wasmRefNullType(struct_large)], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, kExprStructGet, struct_large,
        ...wasmUnsignedLeb(field_index),
        kGCPrefix, kExprStructGet, element_type, 0])
      .exportFunc();

    builder.addFunction(
        "structLargeSet" + field_index,
        makeSig([wasmRefNullType(struct_large)], []))
      .addBody([
        kExprLocalGet, 0,
        kExprI32Const, 42, kGCPrefix, kExprStructNew, element_type,
        kGCPrefix, kExprStructSet, struct_large,
        ...wasmUnsignedLeb(field_index)])
      .exportFunc();
  }

  builder.addFunction("structLargeMake",
                      makeSig([], [wasmRefType(struct_large)]))
      .addBody([kGCPrefix, kExprStructNewDefault, struct_large])
      .exportFunc();

  let instance = builder.instantiate();

  let struct_large_obj = instance.exports.structLargeMake();
  for (let field_index of struct_large_indices) {
    assertTraps(kTrapNullDereference,
      () => instance.exports[
          "structLargeGet" + field_index](null));
    assertTraps(kTrapNullDereference,
      () => instance.exports[
          "structLargeSet" + field_index](null));
    instance.exports["structLargeSet" + field_index](struct_large_obj);
    assertEquals(42, instance.exports["structLargeGet" + field_index](
                         struct_large_obj));
  }
})();
