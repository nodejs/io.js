// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-wasm-lazy-compilation

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addFunction(undefined, kSig_v_a)
  .addBody([
    kExprLocalGet, 0,     // local.get
    kExprRefAsNonNull,    // ref.as_non_null
    kExprLocalGet, 0,     // local.get
    kExprTry, kWasmF64,   // try f64
      ...wasmF64Const(1), // f64.const
    kExprCatchAll,        // catch_all
      ...wasmF64Const(2), // f64.const
      kExprEnd,           // end
    kExprUnreachable,     // unreachable
]);
builder.toModule();
