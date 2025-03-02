// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --liftoff --no-wasm-tier-up --print-code --wasm-staging

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function testPrintCode() {
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, undefined);
  builder
      .addFunction('main', makeSig([kWasmI32, kWasmI32, kWasmF64], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1, kExprI64UConvertI32, kExprLocalGet,
        2, kExprI64SConvertF64, kAtomicPrefix, kExprI64AtomicWait, 3, 0
      ]);
  builder.instantiate();
})();
