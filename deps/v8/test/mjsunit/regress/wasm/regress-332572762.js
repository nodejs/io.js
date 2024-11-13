// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-wasm-lazy-compilation

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(kSig_v_v);
builder.addTable(kWasmFuncRef, 1);
builder.addFunction(undefined, kSig_v_v).addBody([
  kExprI32Const, 0x5b,            // i32.const
  kExprCallIndirect, 0x00, 0x00,  // call_indirect sig #0: v_v
]);
builder.toModule();
