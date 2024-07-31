// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addFunction('foo', kSig_i_i).addBody([kExprLocalGet, 0]);
const module_bytes = builder.toBuffer();

function workerCode1() {
  for (let i = 0; i < 1000; ++i) this.performance.measureMemory();
}
const workerCode2 =
    new Function(`new WebAssembly.Module(new Uint8Array([${module_bytes}]))`);

for (let i = 0; i < 50; i++) {
    new Worker(i % 2 ? workerCode1 : workerCode2, {'type': 'function'});
}
