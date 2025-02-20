// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let sig = makeSig([kWasmStringRef], [kWasmStringRef]);
let imp = builder.addImport('m', 'toLowerCase', kSig_v_v);
builder.addFunction('call_tolower', sig).exportFunc().addBody([
  kExprLocalGet, 0, kExprCallFunction, imp
]);
let module = builder.toModule();
let bound_call = () => Function.prototype.call.bind();
let instance = new WebAssembly.Instance(module, {m: {toLowerCase: bound_call}});
%WasmTriggerTierUpForTesting(instance.exports.call_tolower);
