// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-wrapper-tiering-budget=1

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();
let sig = makeSig([kWasmI64, kWasmExternRef, kWasmExternRef,
    kWasmExternRef, kWasmExternRef, kWasmExternRef], [])
let sig_index = builder.addType(sig);
let fill_newspace_index = builder.addImport('m', 'fill_newspace', kSig_v_v);
let jsfunc_index = builder.addImport('m', 'jsfunc', sig);
builder.addExport("reexported_js_function", jsfunc_index);
builder.addFunction('f', sig)
    .addBody([kExprCallFunction, fill_newspace_index,
              kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
              kExprLocalGet, 3, kExprLocalGet, 4, kExprLocalGet, 5,
              kExprRefFunc, jsfunc_index,
              kExprCallRef, sig_index]).exportFunc();
function fill_newspace() {
  %SimulateNewspaceFull();
}
function jsfunc(bigint, ref1, ref2, ref3, ref4, ref5) {
  console.log(ref1, ref2, ref3, ref4, ref5)
}
let instance = builder.instantiate({m: {fill_newspace, jsfunc}});
instance.exports.f(1n, {}, {}, {}, {}, {});
instance.exports.f(1n, {}, {}, {}, {}, {});
