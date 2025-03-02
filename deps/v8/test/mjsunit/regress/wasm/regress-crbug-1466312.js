// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

builder.addFunction("main", makeSig([kWasmStringRef], [kWasmI32])).exportFunc()
  .addBodyWithEnd([
    kExprLocalGet, 0,
    ...GCInstr(kExprStringAsIter),
    kGCPrefix, kExprRefTestNull, kAnyRefCode,
    kExprEnd,
  ]);
const instance = builder.instantiate();
assertEquals(0, instance.exports.main("foo"));
