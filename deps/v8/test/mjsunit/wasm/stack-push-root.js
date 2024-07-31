// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests that code generator behaves correctly when pushing to the stack an
// operand that is an offset of the root register.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

let sig = makeSig(
    [kWasmAnyRef, kWasmAnyRef, kWasmAnyRef, kWasmAnyRef, kWasmAnyRef,
     kWasmAnyRef],
    [kWasmAnyRef]);
let sig_index = builder.addType(sig);

let callee =
    builder.addFunction('callee', sig_index).addBody([kExprLocalGet, 5]);

builder.addFunction('main', kSig_r_v).exportFunc().addBody([
  kExprRefNull, kAnyRefCode,
  kExprRefNull, kAnyRefCode,
  kExprRefNull, kAnyRefCode,
  kExprRefNull, kAnyRefCode,
  kExprRefNull, kAnyRefCode,
  kExprRefNull, kAnyRefCode,
  kExprCallFunction, callee.index,
  kGCPrefix, kExprExternConvertAny
]);

let instance = builder.instantiate();
assertEquals(null, instance.exports.main());
