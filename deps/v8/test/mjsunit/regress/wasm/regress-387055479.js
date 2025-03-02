// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
const arr = builder.addArray(kWasmI32);
builder.addFunction('createArray', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewDefault, arr,
      kGCPrefix, kExprExternConvertAny
    ])
    .exportFunc();
instance = builder.instantiate();

function f1() {
  for (let i = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaa'; i < 10;);
  return new Uint16Array();
}
function f2() {
  instance.exports.createArray();
  return f1();
}
const arr2 = new Int8Array(10000);
arr2.sort(f2);
