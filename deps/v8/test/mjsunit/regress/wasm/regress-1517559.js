// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags --wasm-wrapper-tiering-budget=2

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
const import_idx = builder.addImport('imp', 'f', kSig_v_v);
builder.addFunction('call_import', kSig_v_v)
    .addBody([kExprCallFunction, import_idx])
    .exportFunc();
const module_bytes = builder.toBuffer();

function workerCode1() {
  for (let i = 0; i < 20; ++i) this.performance.measureMemory();
}
function workerCode2Template(module_bytes) {
  let module = new WebAssembly.Module(new Uint8Array(module_bytes));
  let instance = new WebAssembly.Instance(module, {imp: {f: () => {}}});
  for (let i = 0; i < 10; ++i) {
    instance.exports.call_import();
  }
}
const workerCode2 = new Function(`(${workerCode2Template})([${module_bytes}])`);

for (let i = 0; i < 50; i++) {
    new Worker(i % 2 ? workerCode1 : workerCode2, {'type': 'function'});
}
