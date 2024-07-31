// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $type0 = builder.addStruct([
  makeField(kWasmI8, true),
  makeField(kWasmS128, false)]);
let $type1 = builder.addStruct([
  makeField(kWasmAnyRef, false),
  makeField(kWasmAnyRef, false)]);
let $type6 = builder.addType(makeSig([], []));
builder.addFunction("main", $type6).exportFunc()
  .addBody([
    kExprRefNull, kFuncRefCode,
    kExprRefAsNonNull,
    kGCPrefix, kExprRefCastNull, $type6,
    kExprDrop,
    kExprI32Const, 0,
    kExprI32Const, 0,
    kSimdPrefix, kExprI8x16Splat,
    kGCPrefix, kExprStructNew, $type0,
    kExprRefNull, kAnyRefCode,
    kGCPrefix, kExprStructNew, $type1,
    kGCPrefix, kExprStructGet, $type1, 0,
    kGCPrefix, kExprRefCastNull, $type1,
    kGCPrefix, kExprStructGet, $type1, 1,
    kGCPrefix, kExprRefCast, $type1,
    kExprDrop,
  ]);

const instance = builder.instantiate();
assertTraps(kTrapNullDereference, () => instance.exports.main());
