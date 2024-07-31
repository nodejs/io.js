// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// This is benchmark to investigate at which point it is more efficient to call
// a memcpy-based builtin for array.copy, rather than copying
// element-by-element.
// How to run:
// - Set {iterations} to a high number to get better measurements
// - Change the value of {length} to find point at which the builtin becomes
//   faster.
// - Change {array_type} if you want to test different types.
// Right now, the limit is found to be around 10.
(function ArrayCopyBenchmark() {

  let array_length = 10;
  let iterations = 1;

  var builder = new WasmModuleBuilder();
  let struct_index = builder.addStruct([makeField(kWasmI32, true),
                                        makeField(kWasmI8, false)]);
  // Also try wasmRefNullType(struct_index), other numeric types.
  let array_type = kWasmI32;
  var array_index = builder.addArray(array_type, true);
  var from = builder.addGlobal(wasmRefNullType(array_index), true, false);
  var to = builder.addGlobal(wasmRefNullType(array_index), true, false);

  builder.addFunction("init", kSig_v_v)
    .addBody([
      ...wasmI32Const(array_length),
      kGCPrefix, kExprArrayNewDefault, array_index,
      kExprGlobalSet, from.index,
      ...wasmI32Const(array_length),
      kGCPrefix, kExprArrayNewDefault, array_index,
      kExprGlobalSet, to.index
    ])
    .exportFunc();

  builder.addFunction("array_copy", kSig_v_v)
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprGlobalGet, to.index,
        ...wasmI32Const(0),
        kExprGlobalGet, from.index,
        ...wasmI32Const(0),
        ...wasmI32Const(array_length),
        kGCPrefix, kExprArrayCopy, array_index, array_index,
        // Outer loop: run everything {iterations} times.
        kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add, kExprLocalSet, 0,
        kExprLocalGet, 0, ...wasmI32Const(iterations), kExprI32LtS,
        kExprBrIf, 0,
      kExprEnd])
    .exportFunc();

  builder.addFunction("loop_copy", kSig_v_v)
    .addLocals(wasmRefType(array_index), 2)
    .addLocals(kWasmI32, 2)
    .addBody([
      kExprLoop, kWasmVoid,
        ...wasmI32Const(0),
        kExprLocalSet, 2,
        kExprGlobalGet, from.index, kExprRefAsNonNull, kExprLocalSet, 0,
        kExprGlobalGet, to.index, kExprRefAsNonNull, kExprLocalSet, 1,
        kExprLoop, kWasmVoid,
          kExprLocalGet, 1,  // array
          kExprLocalGet, 2,  // index
          // value
          kExprLocalGet, 0, kExprLocalGet, 2,
          kGCPrefix, kExprArrayGet, array_index,
          // array.set
          kGCPrefix, kExprArraySet, array_index,
          // index++
          kExprLocalGet, 2, kExprI32Const, 1, kExprI32Add, kExprLocalSet, 2,
          // if (index < array_length) goto loop;
          kExprLocalGet, 2, ...wasmI32Const(array_length), kExprI32LtU,
          kExprBrIf, 0,
        kExprEnd,
      // Outer loop: run everything {iterations} times.
      kExprLocalGet, 3, kExprI32Const, 3, kExprI32Add, kExprLocalSet, 3,
      kExprLocalGet, 3, ...wasmI32Const(iterations), kExprI32LtS,
      kExprBrIf, 0,
      kExprEnd])
    .exportFunc();

  var instance = builder.instantiate({});

  instance.exports.init();
  print("Array length: " + array_length + ", #iterations: " + iterations);
  {
    let before = Date.now();
    instance.exports.array_copy();
    let after = Date.now();
    print("array.copy: " + (after - before) + "ms");
  }
  {
    let before = Date.now();
    instance.exports.loop_copy();
    let after = Date.now();
    print("loop copy: " + (after - before) + "ms");
  }
})();

// The following two benchmarks measure the efficiency of array allocation.
// Change the three first parameters of each benchmarks to measure different
// array sizes, iterations, and element types.
(function ArrayNewDefaultBenchmark() {
  let array_length = 10;
  let iterations = 1
  let test_object_type = false;

  var builder = new WasmModuleBuilder();
  let struct_index = builder.addStruct([makeField(kWasmI32, true),
                                        makeField(kWasmI8, true)]);
  let array_type = test_object_type ? wasmRefNullType(struct_index) : kWasmI32;
  var array_index = builder.addArray(array_type, true);

  let array_new = builder.addFunction(
      "array_new", makeSig([], [wasmRefNullType(array_index)]))
    .addBody([
      ...wasmI32Const(array_length),
      kGCPrefix, kExprArrayNewDefault, array_index])
    .exportFunc();

  builder.addFunction("loop_array_new", kSig_v_v)
    .addLocals(wasmRefNullType(array_index), 1)
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprCallFunction, array_new.index,
        kExprLocalSet, 0,
        kExprLocalGet, 1, kExprI32Const, 1, kExprI32Add, kExprLocalTee, 1,
        ...wasmI32Const(iterations), kExprI32LtS,
        kExprBrIf, 0,
      kExprEnd])
    .exportFunc();

  var instance = builder.instantiate({});

  print(`Type: ${test_object_type ? "object" : "i32"}, array length: ` +
        `${array_length}, #iterations: ${iterations}`);
  let before = Date.now();
  instance.exports.loop_array_new();
  let after = Date.now();
  print(`array.new_default: ${after - before} ms`);
})();

(function ArrayNewBenchmark() {
  let array_length = 10;
  let iterations = 1;
  let test_object_type = true;

  var builder = new WasmModuleBuilder();
  let struct_index = builder.addStruct([makeField(kWasmI32, true),
                                        makeField(kWasmI8, true)]);
  let array_type = test_object_type ? wasmRefNullType(struct_index) : kWasmI32;
  var array_index = builder.addArray(array_type, true);

  let array_new = builder.addFunction(
      "array_new", makeSig([], [wasmRefNullType(array_index)]))
    .addBody([
      ...(test_object_type ? [kGCPrefix, kExprStructNewDefault, struct_index]
                           : wasmI32Const(10)),
      ...wasmI32Const(array_length),
      kGCPrefix, kExprArrayNew, array_index])
    .exportFunc();

  builder.addFunction("loop_array_new", kSig_v_v)
    .addLocals(wasmRefNullType(array_index), 1)
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprCallFunction, array_new.index,
        kExprLocalSet, 0,
        kExprLocalGet, 1, kExprI32Const, 1, kExprI32Add, kExprLocalTee, 1,
        ...wasmI32Const(iterations), kExprI32LtS,
        kExprBrIf, 0,
      kExprEnd])
    .exportFunc();

  var instance = builder.instantiate({});

  print(`Type: ${test_object_type ? "object" : "i32"}, array length: ` +
        `${array_length}, #iterations: ${iterations}`);
  let before = Date.now();
  instance.exports.loop_array_new();
  let after = Date.now();
  print(`array.new: ${after - before} ms`);
})();
