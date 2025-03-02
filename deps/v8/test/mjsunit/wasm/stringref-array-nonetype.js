// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

builder.addFunction("test", kSig_v_v).exportFunc().addBody([
  kExprRefNull, kNullRefCode,
  kExprI32Const, 0,
  kExprI32Const, 0,
  ...GCInstr(kExprStringNewWtf16Array),
  kExprDrop,
]);

builder.addFunction("test2", kSig_v_v).exportFunc().addBody([
  kExprRefNull, kNullRefCode,
  kExprRefAsNonNull,
  kExprI32Const, 0,
  kExprI32Const, 0,
  ...GCInstr(kExprStringNewWtf16Array),
  kExprDrop,
]);

assertTrue(WebAssembly.validate(builder.toBuffer()));
assertTraps(kTrapNullDereference, () => builder.instantiate().exports.test());
assertTraps(kTrapNullDereference, () => builder.instantiate().exports.test2());
