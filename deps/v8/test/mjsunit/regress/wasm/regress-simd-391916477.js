// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $mem0 = builder.addMemory(1, 32);

let fillMemorySlot = builder.addFunction('fillMemory', kSig_v_v).exportFunc()
.addBody([
  kExprI32Const, 2,
  kExprI32Const, 42,
  kExprI32StoreMem, 0, 0,
]);

let main0 = builder.addFunction('main', kSig_i_iii).exportFunc().addBody([
  // the index is 1 << ctz(2), which is 2, the same as in the fillMemory above.
  ...wasmI32Const(1),
  ...wasmI32Const(2),
  kExprI32Ctz,
  kExprI32Shl,
  kSimdPrefix, kExprS128LoadMem, 0, 0,
  kSimdPrefix, kExprI32x4ExtractLane, 0,
]);

const wasm = builder.instantiate({}).exports;
wasm.fillMemory();
assertEquals(42, wasm.main(1, 2, 3));
