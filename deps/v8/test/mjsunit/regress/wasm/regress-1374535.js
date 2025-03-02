// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-inlining --no-liftoff

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();
let global = builder.addGlobal(kWasmI32, false, false);
let callee =
    builder.addFunction('callee', kSig_v_v).addBody([kExprLocalGet, 11]);
builder.addFunction('main', kSig_v_v)
    .addBody([kExprCallFunction, callee.index])
    .exportFunc();
assertThrows(
    () => builder.instantiate(), WebAssembly.CompileError,
    /invalid local index: 11/);
