// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestRefTestErrorIndex() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction("ref_test_missing_arg", kSig_i_v)
    .addBody([kGCPrefix, kExprRefTest, struct])

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /\(need 1, got 0\)/);
})();

(function TestRefCastErrorIndex() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction("ref_cast_missing_arg",
                      makeSig([], [wasmRefType(struct)]))
    .addBody([kGCPrefix, kExprRefCast, struct])

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /\(need 1, got 0\)/);
})();
