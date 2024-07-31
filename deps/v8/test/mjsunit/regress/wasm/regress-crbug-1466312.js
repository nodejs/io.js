// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-stringref

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

builder.addFunction("main", kSig_i_v).exportFunc().addBodyWithEnd([
  kExprRefNull, kStringViewIterCode,
  kGCPrefix, kExprRefTestNull, kAnyRefCode,
  kExprEnd,
]);
const instance = builder.instantiate();
assertEquals(0, instance.exports.main());
