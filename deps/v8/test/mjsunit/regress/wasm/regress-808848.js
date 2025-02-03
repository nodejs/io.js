// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Force TurboFan code for serialization.
// Flags: --allow-natives-syntax --no-liftoff --no-wasm-lazy-compilation

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// The number of locals must be greater than the constant defined here:
// https://cs.chromium.org/chromium/src/v8/src/compiler/x64/code-generator-x64.cc?l=3146
const kNumLocals = 128;

function varuint32(val) {
  let bytes = [];
  for (let i = 0; i < 4; ++i) {
    bytes.push(0x80 | ((val >> (7 * i)) & 0x7f));
  }
  bytes.push((val >> (7 * 4)) & 0x7f);
  return bytes;
}

// Generate a function that calls the "get" import `kNumLocals` times, and
// stores each result in a local, then calls the "call" import `kNumLocals`
// times with the stored local values.
//
// The intention is to create a function that has a large stack frame.
let body = [];

for (let i = 0; i < kNumLocals; ++i) {
  body.push(kExprCallFunction, 0, kExprLocalSet, ...varuint32(i));
}

for (let i = 0; i < kNumLocals; ++i) {
  body.push(kExprLocalGet, ...varuint32(i), kExprCallFunction, 1);
}

let builder = new WasmModuleBuilder();
builder.addImport('mod', 'get', kSig_i_v);
builder.addImport('mod', 'call', kSig_v_i);
builder.
  addFunction('main', kSig_v_v).
  addLocals(kWasmI32, kNumLocals).
  addBody(body).
  exportAs('main');
let m1_bytes = builder.toBuffer();
let m1 = new WebAssembly.Module(m1_bytes);

// Serialize the module and postMessage it to another thread.
let serialized_m1 = %SerializeWasmModule(m1);

let worker_onmessage = function({data:msg}) {
  let {serialized_m1, m1_bytes} = msg;

  let m1_clone = %DeserializeWasmModule(serialized_m1, m1_bytes);
  let imports = {mod: {get: () => 3, call: () => {}}};
  let i2 = new WebAssembly.Instance(m1_clone, imports);
  i2.exports.main();
  postMessage('done');
}
let workerScript = "onmessage = " + worker_onmessage.toString();

let worker = new Worker(workerScript, {type: 'string'});
worker.postMessage({serialized_m1, m1_bytes});

// Wait for worker to finish.
print(worker.getMessage());
