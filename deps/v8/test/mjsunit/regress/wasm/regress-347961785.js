// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --noexperimental-wasm-inlining

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

// This should not get inlined.
let f0 = builder.addFunction('f0', kSig_i_i).addBody([kExprLocalGet, 0]);

builder.addFunction("main", makeSig(Array(4).fill(kWasmI32), [kWasmI32]))
  .exportFunc()
  .addBody([
    kExprBlock, kWasmVoid,
      // A := var0 == 1 (false)
      kExprLocalGet, 0,
      kExprI32Const, 1,
      kExprI32Eq,

      // B := var1 != 2 (true)
      kExprLocalGet, 1,
      kExprI32Const, 2,
      kExprI32Ne,

      // C := A || B (true)
      kExprI32Ior,

      // D := C && var2 (true)
      kExprLocalGet, 2,
      kExprI32And,

      // E := f0(0) != 0 (false)
      kExprI32Const, 0,
      kExprCallFunction, f0.index,
      kExprI32Const, 0,
      kExprI32Ne,

      // F := !(D && E) (true)
      kExprI32And,
      kExprI32Eqz,

      // G := var3 != 3 (true)
      kExprLocalGet, 3,
      kExprI32Const, 3,
      kExprI32Ne,

      // H := F && G (true)
      kExprI32And,

      // Break (and return 1) if H is true, return 0 otherwise.
      kExprBrIf, 0,
      kExprI32Const, 0,
      kExprReturn,
    kExprEnd,
    kExprI32Const, 1,
  ]);

let instance = builder.instantiate();

assertEquals(1, instance.exports.main(0, 0, 1, 0));
