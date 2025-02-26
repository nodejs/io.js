// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-wasm-generic-wrapper
// Flags: --wasm-wrapper-tiering-budget=2

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let signatureType = builder.addType(kSig_v_v);
let __v_2 = builder.addImport("m", "f", signatureType);
builder.addExport("f");
let instance = builder.instantiate({ m: { f: () => { } } });
for (let i = 0; i < 3; i += 1) {
  instance.exports.f();
}
