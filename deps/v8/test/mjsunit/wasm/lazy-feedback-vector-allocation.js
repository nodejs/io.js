// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-lazy-compilation --wasm-inlining

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

let callee = builder.addFunction('callee', kSig_v_v).addBody([]);

builder.addFunction('main', kSig_v_v).exportFunc().addBody([
  kExprCallFunction, callee.index,
]);

let inst1 = builder.instantiate();
let inst2 = builder.instantiate();

inst1.exports.main();  // Triggers lazy compilation.
inst2.exports.main();  // Lacks feedback vector.
