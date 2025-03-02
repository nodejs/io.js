// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --wasm-deopt --liftoff
// Flags: --wasm-inlining-factor=15 --allow-natives-syntax --no-jit-fuzzing

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// This test case covers a bug where the deoptimizer didn't correctly count the
// amount of parameter stack slots if there aren't any tagged stack slots
// present and there is at least one S128 parameter passed via stack slots.
// This test case reproduces this exact situation on x64, so all comments assume
// wasm calling conventions on x64.

const builder = new WasmModuleBuilder();
let calleeSig = builder.addType(makeSig([kWasmI32], [kWasmI32]));
let callee0 = builder.addFunction("callee0", calleeSig).addBody([
  kExprLocalGet, 0,
]);
let callee1 = builder.addFunction("callee1", calleeSig).addBody([
  kExprLocalGet, 0,
]);

let $global0 = builder.addGlobal(kWasmI32, true, false, wasmI32Const(0));
let $table0 = builder.addTable(kWasmFuncRef, 2, 2);
let $segment0 = builder.addActiveElementSegment($table0.index, wasmI32Const(0),
  [[kExprRefFunc, callee0.index], [kExprRefFunc, callee1.index]], kWasmFuncRef);

let fctToOptimize = builder.addFunction("fctToOptimize", makeSig(
    [ // Parameters
      // 6 S128 values passed in registers xmm1 - xmm6 (see wasm-linkage.h).
      kWasmS128, kWasmS128, kWasmS128, kWasmS128, kWasmS128, kWasmS128,
      // 1 S128 value passed on the stack needing 2! stack slots.
      kWasmS128,
      // 6 i32 values passed in general purpose registers.
      kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
      // 1 i32 value passed on the stack needing 1 stack slot.
      kWasmI32
    ],
    [kWasmI32]))
  .addBody([
    // Perform a call to either callee0 or callee1 and pass the i32 value from
    // the stack to the callee. Both callees will simply return that value.
    kExprLocalGet, 13,
    kExprGlobalGet, $global0.index,
    kExprTableGet, $table0.index,
    kGCPrefix, kExprRefCast, calleeSig,
    kExprCallRef, calleeSig,
  ]).exportFunc();

builder.addFunction("main", kSig_i_i).addBody([
    kExprLocalGet, 0,  // $var0
    kExprGlobalSet, $global0.index,
    kExprI32Const, 0,
    kSimdPrefix, kExprI8x16Splat,
    kExprI32Const, 0,
    kSimdPrefix, kExprI8x16Splat,
    kExprI32Const, 0,
    kSimdPrefix, kExprI8x16Splat,
    kExprI32Const, 0,
    kSimdPrefix, kExprI8x16Splat,
    kExprI32Const, 0,
    kSimdPrefix, kExprI8x16Splat,
    kExprI32Const, 0,
    kSimdPrefix, kExprI8x16Splat,
    kExprI32Const, 0,
    kSimdPrefix, kExprI8x16Splat,
    kExprI32Const, 2,
    ...wasmI32Const(3),
    ...wasmI32Const(4),
    kExprI32Const, 5,
    ...wasmI32Const(6),
    kExprI32Const, 7,
    // Pass the call index as the last parameter which will be passed via a
    // stack slot.
    kExprLocalGet, 0,
    kExprCallFunction, fctToOptimize.index,
  ]).exportFunc();


const instance = builder.instantiate({});
assertEquals(0, instance.exports.main(0));
%WasmTierUpFunction(instance.exports.fctToOptimize);
assertEquals(1, instance.exports.main(1));
