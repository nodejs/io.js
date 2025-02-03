// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

let sig0 = builder.addType(makeSig([], [kWasmI32]));
let type1 = builder.addArray(kWasmI16, true, kNoSuperType, true);
let type2 = builder.addStruct([makeField(kWasmF32, false), makeField(wasmRefType(kWasmI31Ref), false), makeField(kWasmF32, false)], kNoSuperType, false);

builder.addMemory(16, 32);
builder.addPassiveDataSegment([111, 128, 250, 156]);
builder.addDeclarativeElementSegment([0]);

let $i = 0;
let $j = 1;
builder.addFunction("main", sig0).exportFunc()
  .addLocals(kWasmI32, 2)
  .addBody([
kExprI32Const, 1,
kExprLocalSet, $i,
kExprLoop, kWasmI32,
  kExprRefFunc, 0,
  kExprCallRef, sig0,
  kExprDrop,
  ...wasmF32Const(0),
  kExprBlock, kWasmI32,
    kExprI32Const, 1,
    kExprLocalSet, $j,
    kExprLoop, kWasmI32,
      kExprRefNull, kArrayRefCode,
      kGCPrefix, kExprRefCastNull, type1,
      kExprRefIsNull,
      kExprI32Const, 0,
      kExprI32StoreMem, 1, 0,
      kExprLocalGet, $j,
      kExprI32Const, 1,
      kExprI32Sub,
      kExprLocalTee, $j,
      kExprIf, kWasmVoid,
        kExprRefNull, type1,
        kExprI32Const, 0,  // array index
        kExprI32Const, 0,  // data offset
        kExprI32Const, 1,  // length
        kGCPrefix, kExprArrayInitData, type1, 0,  // segment index
        kExprBr, 1,
      kExprEnd,
      kExprI32Const, 1,
    kExprEnd,
  kExprEnd,
  kGCPrefix, kExprRefI31,
  ...wasmF32Const(0),
  kGCPrefix, kExprStructNew, type2,
  kGCPrefix, kExprRefCastNull, type1,  // array
  kExprI32Const, 1,  // offset
  kExprI32Const, 2,  // value
  kExprI32Const, 1,  // length
  kGCPrefix, kExprArrayFill, type1,
  kExprLocalGet, $i,
  kExprI32Const, 1,
  kExprI32Sub,
  kExprLocalTee, $i,
  kExprIf, kWasmVoid,
    kExprBr, 1,
  kExprEnd,
  kExprI32Const, 1,
kExprEnd,
]);

const instance = builder.instantiate();
assertThrows(() => instance.exports.main(), RangeError,
             "Maximum call stack size exceeded");
%WasmTierUpFunction(instance.exports.main);
