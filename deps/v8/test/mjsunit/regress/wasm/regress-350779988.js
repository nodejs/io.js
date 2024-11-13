// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --turboshaft-wasm --allow-natives-syntax --no-jit-fuzzing
// Flags: --wasm-inlining-factor=15 --wasm-inlining-ignore-call-counts
// Flags: --liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $struct2 = builder.addStruct([], kNoSuperType, false);
let $struct3 = builder.addStruct([], kNoSuperType, false);
let $sig4 = builder.addType(makeSig([
  wasmRefType(kWasmI31Ref), wasmRefType(kWasmI31Ref), wasmRefType(kWasmI31Ref),
  wasmRefType(kWasmI31Ref), wasmRefType(kWasmI31Ref), wasmRefType(kWasmI31Ref),
  wasmRefType(kWasmI31Ref), wasmRefType(kWasmI31Ref), wasmRefType(kWasmI31Ref),
  wasmRefType(kWasmI31Ref), wasmRefType(kWasmI31Ref), wasmRefType(kWasmI31Ref),
  wasmRefType(kWasmI31Ref), kWasmAnyRef, wasmRefType(kWasmI31Ref)], []));
let $sig5 = builder.addType(kSig_v_v);
let $sig6 = builder.addType(makeSig([], [
  kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
  kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
  kWasmI32]));
let $sig8 = builder.addType(kSig_i_i);
let callee_017 = builder.addFunction(undefined, $sig4);
let callee_118 = builder.addFunction(undefined, $sig4);
let inlinee_021 = builder.addFunction(undefined, $sig5);
let inlinee_122 = builder.addFunction(undefined, $sig6);
let inlinee_223 = builder.addFunction(undefined, $sig5);
let main24 = builder.addFunction(undefined, $sig8);
let $global0 = builder.addGlobal(kWasmI32, true, false, wasmI32Const(0));
let $table0 = builder.addTable(kWasmFuncRef, 7, 7);
let $segment0 = builder.addActiveElementSegment($table0.index, wasmI32Const(0),
  [[kExprRefFunc, callee_017.index], [kExprRefFunc, callee_118.index],
   [kExprRefFunc, inlinee_021.index], [kExprRefFunc, inlinee_122.index],
   [kExprRefFunc, inlinee_223.index]], kWasmFuncRef);

callee_017.addBody([]);
callee_118.addBody([]);

// func $inlinee_0: [] -> []
inlinee_021.addBody([
    kExprI32Const, 2,
    kGCPrefix, kExprRefI31,
    ...wasmI32Const(360748584),
    kGCPrefix, kExprRefI31,
    ...wasmI32Const(1280),
    kGCPrefix, kExprRefI31,
    kExprI32Const, 3,
    kGCPrefix, kExprRefI31,
    ...wasmI32Const(68769),
    kGCPrefix, kExprRefI31,
    ...wasmI32Const(276),
    kGCPrefix, kExprRefI31,
    ...wasmI32Const(256017),
    kGCPrefix, kExprRefI31,
    ...wasmI32Const(207741471),
    kGCPrefix, kExprRefI31,
    ...wasmI32Const(107),
    kGCPrefix, kExprRefI31,
    ...wasmI32Const(559108950),
    kGCPrefix, kExprRefI31,
    ...wasmI32Const(458),
    kGCPrefix, kExprRefI31,
    ...wasmI32Const(29907),
    kGCPrefix, kExprRefI31,
    ...wasmI32Const(250806158),
    kGCPrefix, kExprRefI31,
    kExprRefNull, kAnyRefCode,
    ...wasmI32Const(420086),
    kGCPrefix, kExprRefI31,
    kExprGlobalGet, $global0.index,
    kExprTableGet, $table0.index,
    kGCPrefix, kExprRefCast, $sig4,
    kExprCallRef, $sig4,
  ]);

// func $inlinee_1: [] -> [kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
//   kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
//   kWasmI32, kWasmI32, kWasmI32]
inlinee_122.addBody([
    kExprCallFunction, inlinee_021.index,
    kExprI32Const, 1,
    kExprI32Const, 1,
    ...wasmI32Const(1657463),
    ...wasmI32Const(326),
    kExprI32Const, 2,
    kExprI32Const, 1,
    ...wasmI32Const(5642),
    ...wasmI32Const(2095130),
    ...wasmI32Const(29214),
    kExprI32Const, 3,
    kExprI32Const, 19,
    ...wasmI32Const(216774),
    ...wasmI32Const(324),
    ...wasmI32Const(-616433127),
    ...wasmI32Const(238053),
  ]);

// func $inlinee_2: [] -> []
inlinee_223.addBody([
    kExprCallFunction, inlinee_122.index,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
    kExprDrop,
  ]);

// func $main: [kWasmI32] -> [kWasmI32]
main24.addBody([
    kExprLocalGet, 0,  // $var0
    kExprGlobalSet, $global0.index,
    kExprCallFunction, inlinee_223.index,
    ...wasmI32Const(422454),
  ]);

builder.addExportOfKind('call_target_index', kExternalGlobal, $global0.index);
builder.addExport('inlinee_0', inlinee_021.index);
builder.addExport('inlinee_1', inlinee_122.index);
builder.addExport('inlinee_2', inlinee_223.index);
builder.addExport('main', main24.index);
builder.addExport('callee_0', callee_017.index);
builder.addExport('callee_1', callee_118.index);

const instance = builder.instantiate({});
print(instance.exports.main(1, 2, 3));
%WasmTierUpFunction(instance.exports.inlinee_2);
print(instance.exports.main(1, 2, 3));
