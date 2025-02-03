// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.startRecGroup();
let $array0 = builder.addArray(kWasmI8, true, kNoSuperType, true);
builder.endRecGroup();
let $struct2 = builder.addStruct([
  makeField(kWasmStructRef, true)], kNoSuperType, false);
let $struct3 = builder.addStruct([
  makeField(kWasmStructRef, true)], $struct2, false);
let $struct4 = builder.addStruct([
    makeField(kWasmStructRef, true),
    makeField(kWasmI64, false),
    makeField(kWasmI8, false)],
  $struct3, false);
let $array6 = builder.addArray(
  wasmRefNullType($struct3), true, kNoSuperType, false);

let $func19 = builder.addFunction("main", kSig_v_v)
  .addBody([
    kExprRefNull, kAnyRefCode,
    kGCPrefix, kExprRefCastNull, $array0,
    kGCPrefix, kExprRefCastNull, $array6,
    ...wasmI32Const(826276),
    kExprRefNull, $struct3,
    ...wasmI32Const(1992565),
    kGCPrefix, kExprArrayFill, $array6,
  ]).exportFunc();

const instance = builder.instantiate({});
assertTraps(kTrapNullDereference, () => instance.exports.main());
