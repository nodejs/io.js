// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-tier-mask-for-testing=1
// Flags: --enable-testing-opcode-in-wasm

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const builder = new WasmModuleBuilder();
builder.addFunction('main', kSig_v_v).exportFunc().addBody([
  kExprNopForTestingUnsupportedInLiftoff
]);
let instance = builder.instantiate();
%WasmEnterDebugging();
instance.exports.main();
