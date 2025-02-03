// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

builder.addStruct([makeField(wasmRefNullType(kWasmStructRef), true)]);

builder.addType(makeSig([], []));
builder.addType(makeSig([wasmRefNullType(0)], []));

builder.addTable(kWasmFuncRef, 2, 2, undefined);

builder.addFunction(undefined, 1 /* sig */)
  .addBodyWithEnd([
    kExprRefNull, 0x0,  // ref.null
    kExprCallFunction, 0x01,  // call function #1
    kExprEnd,  // end
]);

builder.addFunction(undefined, 2 /* sig */)
  .addBodyWithEnd([
    kExprLocalGet, 0x0,  // local.get
    kGCPrefix, kExprExternConvertAny,  // extern.convert_any
    kExprLocalGet, 0x0,  // local.get
    kGCPrefix, kExprStructGet, 0x00, 0x00,  // struct.get
    kGCPrefix, kExprRefCast, 0x0,  // ref.cast null

    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprEnd,  // end
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertTraps(kTrapNullDereference, instance.exports.main);
