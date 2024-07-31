// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turboshaft-wasm --turboshaft-wasm-instruction-selection-staged
// Flags: --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32);
builder.addFunction(undefined, makeSig([], []))
  .addBody([
kExprF64Const, 0x6b, 0xe5, 0xf4, 0xf6, 0x2e, 0x3f, 0xe1, 0x4e,  // f64.const
kNumericPrefix, kExprI32SConvertSatF64,  // i32.trunc_sat_f64_s
kExprI64Const, 1,
kAtomicPrefix, kExprI64AtomicStore, 0x03, 0xff, 0xfe, 0x01,  // i64.atomic.store
]);

builder.addExport('main', 0);
const instance = builder.instantiate();
try {
  instance.exports.main(1, 2, 3);
} catch (e) {
  assertEquals("RuntimeError: operation does not support unaligned accesses",
               e.toString());
}
