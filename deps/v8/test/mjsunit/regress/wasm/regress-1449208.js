// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-wasm-lazy-compilation

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

{
  const builder = new WasmModuleBuilder();

  builder.addFunction("invalid_negative_heap", kSig_i_i)
    .addBody([kExprRefNull, -0x30 & kLeb128Mask]);

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /Unknown heap type/);
}

{
  const builder = new WasmModuleBuilder();

  // Keep in sync with wasm-limits.h.
  let kWasmMaxTypes = 1000000;

  builder.addFunction("invalid_positive_heap", kSig_i_i)
    .addBody([kExprRefNull, ...wasmSignedLeb(kWasmMaxTypes)]);

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /Type index .* is greater than the maximum number/);
}
