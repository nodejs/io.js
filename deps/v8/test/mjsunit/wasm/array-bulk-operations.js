// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestArrayFillImmutable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let array = builder.addArray(kWasmI32, false);

  // Parameters: array, starting index, value, length.
  builder.addFunction(
      "array_fill",
      makeSig([wasmRefNullType(array), kWasmI32, kWasmI32, kWasmI32], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
              kExprLocalGet, 2, kExprLocalGet, 3,
              kGCPrefix, kExprArrayFill, array])
    .exportFunc();

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /immediate array type #0 is immutable/);
})();

(function TestArrayFill() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let array = builder.addArray(wasmRefNullType(struct), true);
  let array16 = builder.addArray(kWasmI16, true);

  builder.addFunction(
      "make_array", makeSig([kWasmI32], [wasmRefType(array)]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprArrayNewDefault, array])
    .exportFunc();

  builder.addFunction(
      "array_get", makeSig([wasmRefNullType(array), kWasmI32], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
              kGCPrefix, kExprArrayGet, array,
              kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  // Parameters: array, starting index, value in struct, length.
  builder.addFunction(
      "array_fill",
      makeSig([wasmRefNullType(array), kWasmI32, kWasmI32, kWasmI32], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
              kExprLocalGet, 2, kGCPrefix, kExprStructNew, struct,
              kExprLocalGet, 3, kGCPrefix, kExprArrayFill, array])
    .exportFunc();

  builder.addFunction("array_fill_i16", kSig_i_v).exportFunc()
    .addLocals(wasmRefType(array16), 1)
    .addBody([
      kExprI32Const, 4,  // Array length for allocation.
      kGCPrefix, kExprArrayNewDefault, array16,
      kExprLocalTee, 0,  // Array (for fill).
      kExprI32Const, 0,  // Offset (for fill).
      kExprI32Const, 42,  // Value (for fill).
      kExprI32Const, 4,  // Length (for fill).
      kGCPrefix, kExprArrayFill, array16,
      kExprLocalGet, 0,  // Array (for get).
      kExprI32Const, 0,  // Index (for get).
      kGCPrefix, kExprArrayGetS, array16,
    ]);

  let wasm = builder.instantiate().exports;

  assertTraps(kTrapNullDereference, () => wasm.array_fill(null, 10, 20, 30));

  let array_obj = wasm.make_array(8);

  assertTraps(kTrapArrayOutOfBounds,
              () => wasm.array_fill(array_obj, 5, 42, 4));
  // Out-of-bounds array.fill traps even if length is 0, if the index is greater
  // than array.len.
  assertTraps(kTrapArrayOutOfBounds,
              () => wasm.array_fill(array_obj, 10, 42, 0));
  // Overflow of (index + length) traps.
  assertTraps(kTrapArrayOutOfBounds,
              () => wasm.array_fill(array_obj, 5, 42, -1));

  wasm.array_fill(array_obj, 2, 42, 3);
  assertTraps(kTrapNullDereference, () => wasm.array_get(array_obj, 0));
  assertTraps(kTrapNullDereference, () => wasm.array_get(array_obj, 1));
  assertEquals(42, wasm.array_get(array_obj, 2));
  assertEquals(42, wasm.array_get(array_obj, 3));
  assertEquals(42, wasm.array_get(array_obj, 4));
  assertTraps(kTrapNullDereference, () => wasm.array_get(array_obj, 5));
  assertTraps(kTrapNullDereference, () => wasm.array_get(array_obj, 6));
  assertTraps(kTrapNullDereference, () => wasm.array_get(array_obj, 7));

  // Index = array.len and length = 0 works, it just does nothing.
  wasm.array_fill(array_obj, 8, 42, 0)

  wasm.array_fill(array_obj, 4, 54, 2);
  assertTraps(kTrapNullDereference, () => wasm.array_get(array_obj, 0));
  assertTraps(kTrapNullDereference, () => wasm.array_get(array_obj, 1));
  assertEquals(42, wasm.array_get(array_obj, 2));
  assertEquals(42, wasm.array_get(array_obj, 3));
  assertEquals(54, wasm.array_get(array_obj, 4));
  assertEquals(54, wasm.array_get(array_obj, 5));
  assertTraps(kTrapNullDereference, () => wasm.array_get(array_obj, 6));
  assertTraps(kTrapNullDereference, () => wasm.array_get(array_obj, 7));

  assertEquals(42, wasm.array_fill_i16());
})();

(function TestArrayNewNonNullable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let array = builder.addArray(wasmRefType(struct), true);

  builder.addFunction(
      "make_array", makeSig([wasmRefType(struct), kWasmI32],
                            [wasmRefType(array)]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
              kGCPrefix, kExprArrayNew, array])
    .exportFunc();

  builder.addFunction(
      "array_get", makeSig([wasmRefNullType(array), kWasmI32],
                           [wasmRefType(struct)]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
              kGCPrefix, kExprArrayGet, array])
    .exportFunc();

  builder.addFunction(
      "make_struct", makeSig([], [wasmRefType(struct)]))
    .addBody([kGCPrefix, kExprStructNewDefault, struct])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  let length = 50;  // Enough to go through initialization builtin.
  let struct_obj = wasm.make_struct();
  let array_obj = wasm.make_array(struct_obj, length);

  for (let i = 0; i < length; i++) {
    assertEquals(struct_obj, wasm.array_get(array_obj, i));
  }
})();

(function TestArrayInitData() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let array = builder.addArray(kWasmI16, true);

  builder.addMemory(10, 10);

  let passive = builder.addPassiveDataSegment([0, 1, 2, 3, 4, 5]);
  let active = builder.addActiveDataSegment(0, [kExprI32Const, 0],
                                            [6, 7, 8, 9]);

  builder.addFunction(
      "make_array", makeSig([kWasmI32], [wasmRefType(array)]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprArrayNewDefault, array])
    .exportFunc();

  builder.addFunction(
      "init_passive",
      makeSig([wasmRefNullType(array), kWasmI32, kWasmI32, kWasmI32], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
              kExprLocalGet, 3, kGCPrefix, kExprArrayInitData, array, passive])
    .exportFunc();

  builder.addFunction(
      "init_active",
      makeSig([wasmRefNullType(array), kWasmI32, kWasmI32, kWasmI32], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
              kExprLocalGet, 3, kGCPrefix, kExprArrayInitData, array, active])
    .exportFunc();

  builder.addFunction(
      "array_get", makeSig([wasmRefNullType(array), kWasmI32], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
              kGCPrefix, kExprArrayGetS, array])
    .exportFunc();

  builder.addFunction("data_drop", kSig_v_v)
    .addBody([kNumericPrefix, kExprDataDrop, passive])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  let array_length = 10;

  assertTraps(kTrapNullDereference, () => wasm.init_passive(null, 0, 0, 0));

  let array_obj = wasm.make_array(array_length);

  for (i = 0; i < array_length; i++) {
    assertEquals(0, wasm.array_get(array_obj, i));
  }

  // Does nothing.
  wasm.init_active(array_obj, 0, 0, 0);
  // Active segments count as dropped.
  assertTraps(kTrapDataSegmentOutOfBounds,
              () => wasm.init_active(array_obj, 0, 0, 1));
  // Loading 0 bytes from the end of data segment does nothing.
  wasm.init_passive(array_obj, 0, 6, 0);
  // Loading 0 bytes to the end of the array does nothing.
  wasm.init_passive(array_obj, array_length, 0, 0);
  // Loading 0 bytes beyond the end of data segment traps.
  assertTraps(kTrapDataSegmentOutOfBounds,
              () => wasm.init_passive(array_obj, 0, 7, 0));
  // Loading 0 bytes beyond the end of array traps.
  assertTraps(kTrapArrayOutOfBounds,
              () => wasm.init_passive(array_obj, array_length + 1, 1, 1));

  // Out-of-bounds data segment.
  assertTraps(kTrapDataSegmentOutOfBounds,
              () => wasm.init_passive(array_obj, 1, 2, 3));
  // Out-of-bounds data segment with out-of-smi-range segment offset.
  assertTraps(kTrapDataSegmentOutOfBounds,
              () => wasm.init_passive(array_obj, 1, 0x80000000, 0));
  // Out-of-bounds array.
  assertTraps(kTrapArrayOutOfBounds,
              () => wasm.init_passive(array_obj, array_length - 1, 0, 2));
  // Out-of-bounds array with out-of-smi-range length.
  assertTraps(kTrapArrayOutOfBounds,
              () => wasm.init_passive(array_obj, 0, 0, 0x80000000));

  // Load [[1, 2], [3, 4]] at index 5 of array.
  wasm.init_passive(array_obj, 5, 1, 2);

  assertEquals(0, wasm.array_get(array_obj, 0));
  assertEquals(0, wasm.array_get(array_obj, 1));
  assertEquals(0, wasm.array_get(array_obj, 2));
  assertEquals(0, wasm.array_get(array_obj, 3));
  assertEquals(0, wasm.array_get(array_obj, 4));
  // Bytes will be loaded in little-endian order.
  assertEquals(0x0201, wasm.array_get(array_obj, 5));
  assertEquals(0x0403, wasm.array_get(array_obj, 6));
  assertEquals(0, wasm.array_get(array_obj, 7));
  assertEquals(0, wasm.array_get(array_obj, 8));
  assertEquals(0, wasm.array_get(array_obj, 9));

  // Now drop the segment.
  wasm.data_drop();
  assertTraps(kTrapDataSegmentOutOfBounds,
              () => wasm.init_passive(array_obj, 5, 1, 2));
})();

(function TestArrayInitDataLengthOverflow() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let array = builder.addArray(kWasmI64, true);

  let passive = builder.addPassiveDataSegment([0, 1, 2, 3, 4, 5]);

  builder.addFunction(
      "make_array", makeSig([kWasmI32], [wasmRefType(array)]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprArrayNewDefault, array])
    .exportFunc();

  builder.addFunction(
      "init_passive",
      makeSig([wasmRefNullType(array), kWasmI32, kWasmI32, kWasmI32], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
              kExprLocalGet, 3, kGCPrefix, kExprArrayInitData, array, passive])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  let array_length = 10;

  let array_obj = wasm.make_array(array_length);

  // Out-of-bounds array: length is within Smi range, but computation of length
  // in bytes causes 32-bit overflow.
  assertTraps(kTrapArrayOutOfBounds,
              () => wasm.init_passive(array_obj, array_length - 1, 0,
                                      0x20000000));
})();

(function TestArrayInitDataImmutable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let array = builder.addArray(kWasmI16, false);

  let passive = builder.addPassiveDataSegment([0, 1, 2, 3, 4, 5]);

  builder.addFunction(
      "init_passive",
      makeSig([wasmRefNullType(array), kWasmI32, kWasmI32, kWasmI32], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
              kExprLocalGet, 3, kGCPrefix, kExprArrayInitData, array, passive])
    .exportFunc();

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /array.init_data can only be used with mutable arrays/);
})();

(function TestArrayInitElem() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let sig = builder.addType(kSig_i_i);
  let array = builder.addArray(wasmRefNullType(sig), true);

  let table = builder.addTable(wasmRefNullType(sig), 10, 10);

  let elem1 = builder.addFunction("succ", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add]);
  let elem2 = builder.addFunction("pred", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub]);

  let passive = builder.addPassiveElementSegment(
    [[kExprRefNull, sig], [kExprRefFunc, elem1.index],
     [kExprRefFunc, elem2.index], [kExprRefFunc, elem1.index]],
    wasmRefNullType(sig));
  let active = builder.addActiveElementSegment(
    table, [kExprI32Const, 0], [[kExprRefFunc, elem1.index]],
    wasmRefNullType(sig));

  builder.addFunction(
      "make_array", makeSig([kWasmI32], [wasmRefType(array)]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprArrayNewDefault, array])
    .exportFunc();

  builder.addFunction(
      "init_passive",
      makeSig([wasmRefNullType(array), kWasmI32, kWasmI32, kWasmI32], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
              kExprLocalGet, 3, kGCPrefix, kExprArrayInitElem, array, passive])
    .exportFunc();

  builder.addFunction(
      "init_active",
      makeSig([wasmRefNullType(array), kWasmI32, kWasmI32, kWasmI32], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
              kExprLocalGet, 3, kGCPrefix, kExprArrayInitElem, array, active])
    .exportFunc();

  builder.addFunction(
      "array_get", makeSig([wasmRefNullType(array), kWasmI32],
                           [wasmRefNullType(sig)]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
              kGCPrefix, kExprArrayGet, array])
    .exportFunc();

  builder.addFunction("elem_drop", kSig_v_v)
    .addBody([kNumericPrefix, kExprElemDrop, passive])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  let array_length = 10;

  assertTraps(kTrapNullDereference, () => wasm.init_passive(null, 0, 0, 0));

  let array_obj = wasm.make_array(array_length);

  for (i = 0; i < array_length; i++) {
    assertEquals(null, wasm.array_get(array_obj, i));
  }

  // Does nothing.
  wasm.init_active(array_obj, 0, 0, 0);
  // Active segments count as dropped.
  assertTraps(kTrapElementSegmentOutOfBounds,
              () => wasm.init_active(array_obj, 0, 0, 1));
  // Loading 0 elements from the end of element segment does nothing.
  wasm.init_passive(array_obj, 0, 4, 0);
  // Loading 0 elements to the end of the array does nothing.
  wasm.init_passive(array_obj, array_length, 0, 0);
  // Loading 0 elements beyond the end of data segment traps.
  assertTraps(kTrapElementSegmentOutOfBounds,
              () => wasm.init_passive(array_obj, 0, 5, 0));
  // Loading 0 elements beyond the end of array traps.
  assertTraps(kTrapArrayOutOfBounds,
              () => wasm.init_passive(array_obj, array_length + 1, 1, 1));

  // Out-of-bounds element segment.
  assertTraps(kTrapElementSegmentOutOfBounds,
              () => wasm.init_passive(array_obj, 1, 2, 3));
  // Out-of-bounds element segment with out-of-smi-bounds segment offset.
  assertTraps(kTrapElementSegmentOutOfBounds,
              () => wasm.init_passive(array_obj, 1, 0x80000000, 0));
  // Out-of-bounds array.
  assertTraps(kTrapArrayOutOfBounds,
              () => wasm.init_passive(array_obj, array_length - 1, 0, 2));
  // Out-of-bounds array with out-of-smi-bounds length
  assertTraps(kTrapArrayOutOfBounds,
              () => wasm.init_passive(array_obj, array_length - 1, 0,
                                      0x80000000));

  // Load the last three elements of the element segment at index 5.
  wasm.init_passive(array_obj, 5, 1, 3);

  assertEquals(null, wasm.array_get(array_obj, 0));
  assertEquals(null, wasm.array_get(array_obj, 1));
  assertEquals(null, wasm.array_get(array_obj, 2));
  assertEquals(null, wasm.array_get(array_obj, 3));
  assertEquals(null, wasm.array_get(array_obj, 4));
  assertEquals(11, wasm.array_get(array_obj, 5)(10));
  assertEquals(9, wasm.array_get(array_obj, 6)(10));
  assertEquals(11, wasm.array_get(array_obj, 7)(10));
  assertEquals(null, wasm.array_get(array_obj, 8));
  assertEquals(null, wasm.array_get(array_obj, 9));

  // Now drop the segment.
  wasm.elem_drop();
  assertTraps(kTrapElementSegmentOutOfBounds,
              () => wasm.init_passive(array_obj, 5, 1, 3));
})();

(function TestArrayInitElemImmutable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let array = builder.addArray(kWasmFuncRef, false);

  let elem1 = builder.addFunction("succ", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add]);
  let elem2 = builder.addFunction("pred", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub]);

  let passive = builder.addPassiveDataSegment([elem1.index, elem2.index,
                                               elem1.index, elem2.index]);

  builder.addFunction(
      "init_passive",
      makeSig([wasmRefNullType(array), kWasmI32, kWasmI32, kWasmI32], []))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
              kExprLocalGet, 3, kGCPrefix, kExprArrayInitElem, array, passive])
    .exportFunc();

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /array.init_elem can only be used with mutable arrays/);
})();

(function TestArrayCopyLargeI64Array() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let array = builder.addArray(kWasmI64, true);

  builder.addFunction(
      // initial value, length, index to check
      "main", makeSig([kWasmI64, kWasmI32, kWasmI32], [kWasmI64]))
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
              kGCPrefix, kExprArrayNew, array,
              kExprLocalGet, 2, kGCPrefix, kExprArrayGet, array])
    .exportFunc();

  let instance = builder.instantiate();

  assertEquals(1234n, instance.exports.main(1234n, 1000, 0));
  assertEquals(-2345n, instance.exports.main(-2345n, 2000, 1000));
  assertEquals(42n, instance.exports.main(42n, 2000, 1999));
})();
