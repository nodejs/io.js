// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --allow-natives-syntax
// Flags: --wasm-staging

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();
let kSig_i_ri = makeSig([kWasmExternRef, kWasmI32], [kWasmI32]);
kStringCharCodeAt =
    builder.addImport('wasm:js-string', 'charCodeAt', kSig_i_ri);
let simulate_newspace_full = builder.addImport(
    'm', 'simulate_newspace_full', kSig_v_v);
builder.addFunction("main", kSig_i_ri)
  .exportFunc()
  .addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprCallFunction, simulate_newspace_full,
    kExprCallFunction, kStringCharCodeAt,
  ]);
let kBuiltins = {
  builtins: ["js-string"]
};
var instance = builder.instantiate(
    {m: {simulate_newspace_full: () => %SimulateNewspaceFull() } },
    kBuiltins);
let wrapper = WebAssembly.promising(instance.exports.main);
assertPromiseResult(
    wrapper(%ConstructConsString("abcdefg", "hijklm"), 0),
    v => assertEquals(v, 97));
