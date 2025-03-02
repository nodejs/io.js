// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

function f0() {
  d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
  let builder = new WasmModuleBuilder();
  builder.addFunction("w0", kSig_v_v).addBody([
      kExprLoop, kWasmVoid,
      kExprBr, 0,
      kExprEnd
  ]).exportFunc();
  const v6 = builder.instantiate();
  const v7 = v6.exports;
  let v9 = WebAssembly.promising(v7.w0);
  postMessage("start");
  v9();
}
const o13 = {
    "type": "function",
};
let worker = new Worker(f0, o13);
assertEquals("start", worker.getMessage());
worker.terminateAndWait();
