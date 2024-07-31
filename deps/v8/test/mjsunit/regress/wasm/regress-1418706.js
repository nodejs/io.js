// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-wasm-lazy-compilation

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addFunction(undefined, kSig_v_v)
  .addBodyWithEnd([
    // Invalid (overly long) GC opcode.
    kGCPrefix, 0xff, 0xff, 0x7f
  ]);
assertThrows(
    () => builder.toModule(), WebAssembly.CompileError,
    /Invalid prefixed opcode/);
