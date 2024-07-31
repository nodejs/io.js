// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder1 = new WasmModuleBuilder();
builder1.addGlobal(kWasmS128, false, false, wasmS128Const(0, 0))
  .exportAs("mv128");
let instance1 = builder1.instantiate();

let builder2 = new WasmModuleBuilder();
builder2.addImportedGlobal("imports", "mv128", kWasmS128, false);
let instance2 = builder2.instantiate({ imports: instance1.exports });
