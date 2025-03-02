// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-lazy-validation --wasm-lazy-compilation
// Flags: --fuzzing

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder.addFunction("main", kSig_i_v).addBody([]).exportFunc();
let instance = builder.instantiate();
%WasmTierUpFunction(instance.exports.main);
