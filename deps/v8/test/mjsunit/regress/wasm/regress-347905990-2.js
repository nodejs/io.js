// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turboshaft-wasm

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestLoopUnrolling() {
  var builder = new WasmModuleBuilder();

  builder.addFunction("main", makeSig([kWasmI32], [kWasmI32]))
    .addLocals(kWasmI32, 2)
    .addBody([
      // while (true)
      kExprBlock, kWasmVoid,
        kExprLoop, kWasmVoid,
          // if (param[0] == 0) break;
          kExprLocalGet, 0,
          kExprI32Eqz,
          kExprBrIf, 1,
          // --param[0];
          kExprLocalGet, 0,
          kExprI32Const, 1,
          kExprI32Sub,
          kExprLocalSet, 0,
          // ++local[1];
          kExprLocalGet, 1,
          kExprI32Const, 1,
          kExprI32Add,
          kExprLocalSet, 1,
          // continue;
          kExprBr, 0,
        kExprEnd,
      kExprEnd,

      // while (true)
      kExprLoop, kWasmVoid,
        // if (local[1] == 0) return 42;
        kExprLocalGet, 1,
        kExprI32Eqz,
        kExprIf, kWasmVoid,
          kExprI32Const, 42,
          kExprReturn,
        kExprEnd,
        // ++local[2];
        kExprLocalGet, 2,
        kExprI32Const, 1,
        kExprI32Add,
        // if (local[2] == 5) return local[1];
        kExprLocalTee, 2,
        kExprI32Const, 5,
        kExprI32Eq,
        kExprIf, kWasmVoid,
          kExprLocalGet, 1,
          kExprReturn,
        kExprEnd,
        // continue;
        kExprBr, 0,
      kExprEnd,

      kExprUnreachable,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(10, wasm.main(10));
})();
