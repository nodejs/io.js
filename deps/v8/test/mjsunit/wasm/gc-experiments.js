// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-ref-cast-nop

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestRefCastNop() {
  var builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction("main", kSig_i_i)
    .addLocals(kWasmStructRef, 1)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kExprLocalSet, 1,
      kExprLocalGet, 1,
      kGCPrefix, kExprRefCastNop, struct,
      kGCPrefix, kExprStructGet, struct, 0,
  ]).exportFunc();

  var instance = builder.instantiate();
  assertEquals(42, instance.exports.main(42));
})();
