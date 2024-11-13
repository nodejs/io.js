// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --allow-natives-syntax
// Flags: --liftoff --wasm-deopt --wasm-inlining-ignore-call-counts

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $array6 = builder.addArray(kWasmI32, true, kNoSuperType, false);
let $sig8 = builder.addType(makeSig([], []));
let $sig9 = builder.addType(makeSig([kWasmF64, kWasmF64, kWasmF64, kWasmF64, kWasmF64, kWasmF64, kWasmF64, kWasmS128], []));
let $sig10 = builder.addType(makeSig([], [wasmRefType($array6)]));
let $sig11 = builder.addType(kSig_i_i);
let callee_017 = builder.addFunction(undefined, $sig8);
let callee_118 = builder.addFunction(undefined, $sig8);
let inlinee_022 = builder.addFunction(undefined, $sig9);
let inlinee_123 = builder.addFunction(undefined, $sig10);
let main24 = builder.addFunction(undefined, $sig11);
let $global0 = builder.addGlobal(kWasmI32, true, false, wasmI32Const(0));
let $table0 = builder.addTable(kWasmFuncRef, 7, 7);
let $segment0 = builder.addActiveElementSegment($table0.index, wasmI32Const(0),
  [[kExprRefFunc, callee_017.index], [kExprRefFunc, callee_118.index]],
  kWasmFuncRef);

// func $callee_0: [] -> []
callee_017.addBody([
  ]);

// func $callee_1: [] -> []
callee_118.addBody([
  ]);


// This function has 6 parameters passed via fp registers (on x86-64).
// The two additional parameters are passed via stack slots.
// The needed size for these stack slots is 3 (1 stack slot for the f64 and 2
// stack slots for the v128), however the v128 stack slots are aligned in the
// wasm calling convention, so we end up with 4 total stack slots.
// func $inlinee_0: [
//   kWasmF64,        // xmm1
//   kWasmF64,        // xmm2
//   kWasmF64,        // xmm3
//   kWasmF64,        // xmm4
//   kWasmF64,        // xmm5
//   kWasmF64,        // xmm6
//   kWasmF64,        // stack_slot 0 + 1 gap stack slot
//   kWasmS128        // stack_slot 2-3]
// ] -> []
inlinee_022.addBody([
    kExprGlobalGet, $global0.index,
    kExprTableGet, $table0.index,
    kGCPrefix, kExprRefCast, $sig8,
    kExprCallRef, $sig8,
  ]);

// func $inlinee_1: [] -> [wasmRefType($array6)]
inlinee_123.addBody([
    ...wasmF64Const(1),
    ...wasmF64Const(2),
    ...wasmF64Const(3),
    ...wasmF64Const(4),
    ...wasmF64Const(5),
    ...wasmF64Const(6),
    ...wasmF64Const(7),
    kExprI32Const, 8,
    kSimdPrefix, kExprI8x16Splat,
    kExprCallFunction, inlinee_022.index,
    kExprI32Const, 13,
    ...wasmI32Const(1),
    kExprI32Const, 1,
    kExprI32Add,
    kGCPrefix, kExprArrayNew, $array6,
  ]);

// func $main: [kWasmI32] -> [kWasmI32]
main24.addBody([
    kExprLocalGet, 0,  // $var0
    kExprGlobalSet, $global0.index,
    kExprCallFunction, inlinee_123.index,
    kExprDrop,
    ...wasmI32Const(994),
  ]);

builder.addExportOfKind('call_target_index', kExternalGlobal, $global0.index);
builder.addExport('inlinee_0', inlinee_022.index);
builder.addExport('inlinee_1', inlinee_123.index);
builder.addExport('main', main24.index);
builder.addExport('callee_0', callee_017.index);
builder.addExport('callee_1', callee_118.index);

const instance = builder.instantiate({});
print(instance.exports.main(0));
%WasmTierUpFunction(instance.exports.inlinee_1);
print(instance.exports.main(1));
