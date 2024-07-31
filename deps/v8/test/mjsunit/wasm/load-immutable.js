// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Test that comparisons with array length in a loop get optimized away.
(function ArrayLoopOptimizationTest() {
  var builder = new WasmModuleBuilder();
  var array_index = builder.addArray(kWasmI32, true);

  // Increase these parameters to measure performance.
  let array_size = 10; // 100000000;
  let iterations = 1; // 50;

  builder.addFunction("array_inc", kSig_v_v)
    .addLocals(wasmRefType(array_index), 1)
    .addLocals(kWasmI32, 2)
    // Locals: 0 -> array, 1 -> length, 2 -> index
    .addBody([
      ...wasmI32Const(array_size),
      kExprCallFunction, 1,
      kExprLocalSet, 0,

      // length = array.length
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayLen,
      kExprLocalSet, 1,

      // while (true) {
      kExprLoop, kWasmVoid,
        // if (index < length) {
        kExprLocalGet, 2,
        kExprLocalGet, 1,
        kExprI32LtU,
        kExprIf, kWasmVoid,
          // array[index] = array[index] + 5;
          kExprLocalGet, 0,
          kExprLocalGet, 2,
          kExprLocalGet, 0,
          kExprLocalGet, 2,
          kGCPrefix, kExprArrayGet, array_index,
          kExprI32Const, 5,
          kExprI32Add,
          kGCPrefix, kExprArraySet, array_index,
          // index = index + 1;
          kExprLocalGet, 2,
          kExprI32Const, 1,
          kExprI32Add,
          kExprLocalSet, 2,
          // continue;
          kExprBr, 1,
        // }
        // break;
        kExprEnd,
      // }
      kExprEnd])
    .exportFunc();

  builder.addFunction("make_array",
                      makeSig([kWasmI32], [wasmRefType(array_index)]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprArrayNewDefault, array_index])

  var instance = builder.instantiate({});

  let before = Date.now();
  for (let i = 0; i < iterations; i++) {
    instance.exports.array_inc();
  }
  let after = Date.now();
  print(
    "Average of " + iterations + " runs: " +
    (after - before)/iterations + "ms");
})();

(function ImmutableLoadThroughEffect() {
  var builder = new WasmModuleBuilder();
  var struct = builder.addStruct([
    makeField(kWasmI32, false), makeField(kWasmI32, true)]);

  let effect = builder.addImport('m', 'f', kSig_v_v);

  builder.addFunction("main", kSig_i_i)
    .addLocals(wasmRefType(struct), 1)
    .addBody([
      // Initialize an object
      kExprLocalGet, 0,
      kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add,
      kGCPrefix, kExprStructNew, struct,
      kExprLocalSet, 1,
      // Introduce unknown effect
      kExprCallFunction, effect,
      // TF should be able to eliminate this load...
      kExprLocalGet, 1,
      kGCPrefix, kExprStructGet, struct, 0,
      // ... but not this one.
      kExprLocalGet, 1,
      kGCPrefix, kExprStructGet, struct, 1,
      kExprI32Add
    ])
    .exportFunc();

  var instance = builder.instantiate({m : { f: function () {} }});

  assertEquals(85, instance.exports.main(42));
})();

(function FunctionTypeCheckThroughEffect() {
  var builder = new WasmModuleBuilder();
  var sig = builder.addType(kSig_i_i);

  let effect = builder.addImport('m', 'f', kSig_v_v);

  builder.addFunction("input", sig)
    .addBody([kExprLocalGet, 0])
    .exportFunc();

  builder.addFunction("main", makeSig([wasmRefType(kWasmFuncRef)], [kWasmI32]))
    .addBody([
      // Type check the function
      kExprLocalGet, 0, kGCPrefix, kExprRefCast, sig,
      kExprDrop,
      // Introduce unknown effect
      kExprCallFunction, effect,
      // TF should be able to eliminate the second type check, and return the
      // constant 1.
      kExprLocalGet, 0,
      kGCPrefix, kExprRefTest, sig])
    .exportFunc();

  var instance = builder.instantiate({m : { f: function () {} }});

  assertEquals(1, instance.exports.main(instance.exports.input));
})();
