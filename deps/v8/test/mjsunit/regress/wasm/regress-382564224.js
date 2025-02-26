// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-wasm-in-js-inlining

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addExport('boom', builder.addImport('m', 'f', kSig_v_i));
const {boom} = builder.instantiate({'m': {'f': () => {}}}).exports;
function call_export() {
  boom();
}
%PrepareFunctionForOptimization(call_export);
for (let i = 0; i < 10; ++i) call_export();
%OptimizeFunctionOnNextCall(call_export);
call_export();
