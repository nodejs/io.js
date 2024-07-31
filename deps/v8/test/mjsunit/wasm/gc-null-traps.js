// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestNullDereferences() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([
      makeField(kWasmI32, true), makeField(kWasmI64, true),
      makeField(kWasmF64, true), makeField(kWasmF32, true)]);
  let struct_ref =
      builder.addStruct([makeField(wasmRefNullType(struct), true)]);
  let array = builder.addArray(kWasmI64, true);
  let array_ref = builder.addArray(wasmRefNullType(struct), true);
  let sig = builder.addType(kSig_i_i);

  for (let field_type of [[0, kWasmI32], [1, kWasmI64],
                          [2, kWasmF64], [3, kWasmF32]]) {
    builder.addFunction(
        "structGet" + field_type[0],
        makeSig([wasmRefNullType(struct)], [field_type[1]]))
      .addBody([
          kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct, field_type[0]])
      .exportFunc();

    builder.addFunction(
        "structSet" + field_type[0],
        makeSig([wasmRefNullType(struct), field_type[1]], []))
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
                kGCPrefix, kExprStructSet, struct, field_type[0]])
      .exportFunc();
  }

  builder.addFunction(
      "structRefGet", makeSig([wasmRefNullType(struct_ref)],
                              [wasmRefNullType(struct)]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct_ref, 0])
    .exportFunc();

  builder.addFunction(
      "structRefSet", makeSig(
          [wasmRefNullType(struct_ref), wasmRefNullType(struct)], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
              kGCPrefix, kExprStructSet, struct_ref, 0])
    .exportFunc();

  builder.addFunction(
      "arrayGet", makeSig([wasmRefNullType(array), kWasmI32], [kWasmI64]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
              kGCPrefix, kExprArrayGet, array])
    .exportFunc();

  builder.addFunction(
      "arraySet", makeSig([wasmRefNullType(array), kWasmI32, kWasmI64], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
              kGCPrefix, kExprArraySet, array])
    .exportFunc();

  builder.addFunction(
      "arrayRefGet", makeSig([wasmRefNullType(array_ref), kWasmI32],
                             [wasmRefNullType(struct)]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
              kGCPrefix, kExprArrayGet, array_ref])
    .exportFunc();

  builder.addFunction(
      "arrayRefSet", makeSig(
          [wasmRefNullType(array_ref), kWasmI32, wasmRefNullType(struct)], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
              kGCPrefix, kExprArraySet, array_ref])
    .exportFunc();

  builder.addFunction(
      "arrayLen", makeSig([wasmRefNullType(array)], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprArrayLen])
    .exportFunc();

  builder.addFunction(
      "arrayCopy",
      makeSig([wasmRefNullType(array), wasmRefNullType(array)], []))
    .addBody([kExprLocalGet, 0, kExprI32Const, 10,
              kExprLocalGet, 1, kExprI32Const, 20,
              kExprI32Const, 30,
              kGCPrefix, kExprArrayCopy, array, array])
    .exportFunc();

  builder.addFunction(
      "arrayFill",
      makeSig([wasmRefNullType(array), kWasmI64], []))
    .addBody([kExprLocalGet, 0, kExprI32Const, 0, kExprLocalGet, 1,
              kExprI32Const, 10, kGCPrefix, kExprArrayFill, array])
    .exportFunc();

  builder.addFunction(
      "callFuncRef", makeSig([wasmRefNullType(sig), kWasmI32], [kWasmI32]))
    .addBody([kExprLocalGet, 1, kExprLocalGet, 0, kExprCallRef, sig])
    .exportFunc();

  builder.addFunction(
      "refAsNonNullStruct",
      makeSig([wasmRefNullType(struct)], [wasmRefType(struct)]))
    .addBody([kExprLocalGet, 0, kExprRefAsNonNull])
    .exportFunc();

  builder.addFunction(
      "refAsNonNullFunction",
      makeSig([wasmRefNullType(sig)], [wasmRefType(sig)]))
    .addBody([kExprLocalGet, 0, kExprRefAsNonNull])
    .exportFunc();

  builder.addFunction(
      "refAsNonNullAny",
      makeSig([kWasmAnyRef], [wasmRefType(kWasmAnyRef)]))
    .addBody([kExprLocalGet, 0, kExprRefAsNonNull])
    .exportFunc();

  builder.addFunction(
      "refAsNonNullI31",
      makeSig([kWasmI31Ref], [wasmRefType(kWasmI31Ref)]))
    .addBody([kExprLocalGet, 0, kExprRefAsNonNull])
    .exportFunc();

  builder.addFunction(
      "refAsNonNullExtern",
      makeSig([kWasmExternRef], [wasmRefType(kWasmExternRef)]))
    .addBody([kExprLocalGet, 0, kExprRefAsNonNull])
    .exportFunc();

  let instance = builder.instantiate();

  assertTraps(kTrapNullDereference, () => instance.exports.structGet0(null));
  assertTraps(kTrapNullDereference,
              () => instance.exports.structSet0(null, 15));
  assertTraps(kTrapNullDereference, () => instance.exports.structGet1(null));
  assertTraps(kTrapNullDereference,
              () => instance.exports.structSet1(null, 15n));
  assertTraps(kTrapNullDereference, () => instance.exports.structGet2(null));
  assertTraps(kTrapNullDereference,
              () => instance.exports.structSet2(null, 15.0));
  assertTraps(kTrapNullDereference, () => instance.exports.structGet3(null));
  assertTraps(kTrapNullDereference,
              () => instance.exports.structSet3(null, 15.0));
  assertTraps(kTrapNullDereference, () => instance.exports.structRefGet(null));
  assertTraps(kTrapNullDereference,
              () => instance.exports.structRefSet(null, null));

  assertTraps(kTrapNullDereference, () => instance.exports.arrayGet(null, 0));
  assertTraps(kTrapNullDereference,
              () => instance.exports.arrayGet(null, 2000000000));
  assertTraps(kTrapNullDereference,
              () => instance.exports.arraySet(null, 0, 42n));
  assertTraps(kTrapNullDereference,
              () => instance.exports.arraySet(null, 2000000000, 42n));
  assertTraps(kTrapNullDereference,
              () => instance.exports.arrayRefGet(null, 0));
  assertTraps(kTrapNullDereference,
              () => instance.exports.arrayRefGet(null, 2000000000));
  assertTraps(kTrapNullDereference,
              () => instance.exports.arrayRefSet(null, 0, null));
  assertTraps(kTrapNullDereference,
              () => instance.exports.arrayRefSet(null, 2000000000, null));
  assertTraps(kTrapNullDereference, () => instance.exports.arrayLen(null));
  assertTraps(kTrapNullDereference,
              () => instance.exports.arrayCopy(null, null));
  assertTraps(kTrapNullDereference,
              () => instance.exports.arrayFill(null, 42n));
  assertTraps(kTrapNullDereference,
              () => instance.exports.callFuncRef(null, 42));
  assertTraps(kTrapNullDereference,
              () => instance.exports.refAsNonNullStruct(null));
  assertTraps(kTrapNullDereference,
              () => instance.exports.refAsNonNullFunction(null));
  assertTraps(kTrapNullDereference,
              () => instance.exports.refAsNonNullAny(null));
  assertEquals(42, instance.exports.refAsNonNullAny(42));
  assertTraps(kTrapNullDereference,
              () => instance.exports.refAsNonNullI31(null));
  assertEquals(42, instance.exports.refAsNonNullI31(42));
  assertTraps(kTrapNullDereference,
              () => instance.exports.refAsNonNullExtern(null));
  let object = {};
  assertEquals(object, instance.exports.refAsNonNullExtern(object));
})();
