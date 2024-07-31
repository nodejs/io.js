// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

// Force different type indices in both modules.
let dummy = builder.addStruct([]);
let struct = builder.addStruct([makeField(kWasmI32, false)]);
let creatorAnySig = builder.addType(makeSig([], [kWasmAnyRef]));
let funcSig = builder.addType(makeSig([wasmRefType(creatorAnySig)],
                                      [kWasmExternRef]));
let exportedAny = builder.addFunction("exportedAny", funcSig)
  .addBody([
    kExprLocalGet, 0,
    kExprCallRef, creatorAnySig,
    kGCPrefix, kExprExternConvertAny,
  ])

builder.addFunction("createStruct", creatorAnySig)
  .addBody([kExprI32Const, 12, kGCPrefix, kExprStructNew, struct])
  .exportFunc();

builder.addFunction("refFunc", makeSig([], [wasmRefType(funcSig)]))
  .addBody([kExprRefFunc, exportedAny.index])
  .exportFunc();

builder.addDeclarativeElementSegment([exportedAny.index]);

let instance = builder.instantiate();
let wasm = instance.exports;

let wasm2 = (function () {
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, false)]);
  let creatorAnySig = builder.addType(makeSig([], [kWasmAnyRef]));
  let funcSig = builder.addType(makeSig([wasmRefType(creatorAnySig)],
                                        [kWasmExternRef]));
  builder.addFunction("exportedAny", funcSig)
    .addBody([
      kExprLocalGet, 0,
      kExprCallRef, creatorAnySig,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction("createStruct", creatorAnySig)
    .addBody([kExprI32Const, 12, kGCPrefix, kExprStructNew, struct])
    .exportFunc();

  let instance = builder.instantiate();
  let wasm = instance.exports;
  // In case we have cached the wrapper when creating it for the previous
  // module, it should still work here, despite the funcSig type being in a
  // different index.
  wasm.exportedAny(wasm.createStruct);
  return wasm;
})();

// The intervening module compilation might overwrite export wrappers. This is
// fine as long as wrappers remain identical for canonically identical types.
wasm.refFunc()(wasm.createStruct);
// It should also work with the struct exported by the other module.
wasm.refFunc()(wasm2.createStruct);
