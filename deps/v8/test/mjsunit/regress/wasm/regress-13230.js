// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

builder.addMemory(1, 2);

let callee = builder.addFunction('callee', kSig_v_v).addBody([kExprNop]);

builder.addDeclarativeElementSegment([callee.index]);

let main_func = builder.addFunction('main', kSig_v_v).exportFunc().addBody([
  kExprRefFunc, callee.index,
  kExprCallRef, callee.type_index,
  kExprI32Const, 0,
  kExprI32LoadMem, 0, 0,
  kExprDrop,
]);

let instance = builder.instantiate();
let main = instance.exports.main;

for (let i = 0; i < 20; i++) main();
%WasmTierUpFunction(main);
main();
