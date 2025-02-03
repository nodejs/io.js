// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptMinimalCallRef() {
  var builder = new WasmModuleBuilder();

  builder.addFunction("isArray", makeSig([kWasmAnyRef], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprRefTest, kArrayRefCode])
    .exportFunc();
  builder.addFunction("isStruct", makeSig([kWasmAnyRef], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprRefTest, kStructRefCode])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  let array = %WasmArray();
  let struct = %WasmStruct();
  assertEquals(1, wasm.isArray(array));
  assertEquals(0, wasm.isArray(struct));
  assertEquals(0, wasm.isStruct(array));
  assertEquals(1, wasm.isStruct(struct));
})();
