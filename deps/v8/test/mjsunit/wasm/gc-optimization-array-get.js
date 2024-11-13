// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-wasm-lazy-compilation
// Flags: --no-experimental-wasm-inlining --no-wasm-loop-unrolling
// Flags: --no-wasm-loop-peeling

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestArrayGetTypeInference() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let array = builder.addArray(wasmRefType(struct), true);

  builder.addFunction("arrayGetSameType",
    makeSig([kWasmI32, wasmRefNullType(array)], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprIf, kStructRefCode,
        kExprLocalGet, 1,
        kExprI32Const, 0,
        kGCPrefix, kExprArrayGet, array,
      kExprElse,
        kExprLocalGet, 1,
        kExprI32Const, 1,
        kGCPrefix, kExprArrayGet, array,
      kExprEnd,
      // This is always true.
      kGCPrefix, kExprRefTest, struct,
    ])
    .exportFunc();

  builder.addFunction("arrayGetDifferentType",
    makeSig([kWasmI32, wasmRefNullType(array)], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprIf, kStructRefCode,
        kExprRefNull, kStructRefCode,
      kExprElse,
        kExprLocalGet, 1,
        kExprI32Const, 1,
        kGCPrefix, kExprArrayGet, array,
      kExprEnd,
      // This cannot be optimized away.
      kGCPrefix, kExprRefTest, struct,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  assertTraps(kTrapNullDereference,
    () => instance.exports.arrayGetSameType(1, null));
  assertEquals(0, instance.exports.arrayGetDifferentType(1, null));
  assertTraps(kTrapNullDereference,
    () => instance.exports.arrayGetSameType(0, null));
})();
