// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-enable-avx

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder1 = new WasmModuleBuilder();
let noop = builder1.addFunction("noop", kSig_v_v)
  .addBody([]).exportFunc().index;
let instance1 = builder1.instantiate();

let builder2 = new WasmModuleBuilder();
let noop_import = builder2.addImport("m", "i", kSig_v_v);
builder2.addFunction("main", kSig_d_v)
.addLocals(kWasmS128, 1)
.addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 0,
    // Do a call to spill the params, make it a cross-instance call to prevent
    // inlining.
    kExprCallFunction, noop_import,
    ...SimdInstr(kExprF64x2Add),
    kSimdPrefix, kExprF64x2ExtractLane, 0x00,
]).exportFunc();
let instance2 = builder2.instantiate({m: {i: instance1.exports.noop}});

instance2.exports.main();
