// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var instance = (function () {
  let builder = new WasmModuleBuilder();

  let struct_index = builder.addStruct([makeField(kWasmI32, true)]);
  let struct_ref = wasmRefType(struct_index);

  let gc_function_index =
    builder.addImport("imports", "gc_func", kSig_v_v);

  let struct_consumer =
    builder.addFunction("struct_consumer", makeSig([
        struct_ref, struct_ref], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct_index, 0,
        kExprLocalGet, 1, kGCPrefix, kExprStructGet, struct_index, 0,
        kExprI32Add]);

  let many_params = builder.addFunction("many_params", makeSig([
      kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
      struct_ref, struct_ref], [kWasmI32]))
    .addBody([
      kExprCallFunction, gc_function_index,
      kExprLocalGet, 7, kExprLocalGet, 8,
      kExprCallFunction, struct_consumer.index]);

  builder.addFunction("main", kSig_i_v)
    .addBody([
      // Seven i32 parameters that look like tagged pointers.
      ...wasmI32Const(1), ...wasmI32Const(3), ...wasmI32Const(5),
      ...wasmI32Const(7), ...wasmI32Const(9), ...wasmI32Const(11),
      ...wasmI32Const(13),
      // Two structs (i.e. actual tagged pointers).
      ...wasmI32Const(20),
      kGCPrefix, kExprStructNew, struct_index,
      ...wasmI32Const(22),
      kGCPrefix, kExprStructNew, struct_index,
      kExprCallFunction, many_params.index,
    ])
    .exportFunc();

  return builder.instantiate({imports: {
    gc_func: () => gc(),
  }});
})();

assertEquals(42, instance.exports.main());
