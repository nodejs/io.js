// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function Regress14689() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([]);
  let except = builder.addTag(kSig_v_v);
  builder.addFunction("main", kSig_v_v)
      .addBody([
        kExprTryTable, kWasmRef, struct, 1,
        kCatchNoRef, except, 0,
        kExprBr, 1,
        kExprEnd,
        kExprUnreachable,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertDoesNotThrow(instance.exports.main);
})();
