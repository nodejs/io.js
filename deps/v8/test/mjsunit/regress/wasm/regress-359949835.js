// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig0 = builder.addType(makeSig([kWasmFuncRef], []));
let func0 = builder.addImport('q', 'func', $sig0);

builder.addExport('main', func0);

function f23(a24) {
    try {
      typeof a24.arguments[0];
    } catch(e) {}
}
const o27 = {
    "func": f23,
};
const o28 = {
    "q": o27,
};
const v31 = builder.instantiate(o28).exports.main;
v31(v31);
