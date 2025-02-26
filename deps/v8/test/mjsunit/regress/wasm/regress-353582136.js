// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --wasm-staging
// Flags: --wasm-inlining-ignore-call-counts --liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let sigVoid = builder.addType(kSig_v_v);
let SigManyParams = builder.addType(makeSig([
  kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32], []));

let callee_0 = builder.addFunction("callee_0", sigVoid).addBody([]);
let callee_1 = builder.addFunction("callee_1", sigVoid).addBody([]);

let $global0 = builder.addGlobal(kWasmI32, true, false, wasmI32Const(0));
let $table0 = builder.addTable(kWasmFuncRef, 8, 8);
let $segment0 = builder.addActiveElementSegment($table0.index, wasmI32Const(0),
  [[kExprRefFunc, callee_0.index], [kExprRefFunc, callee_1.index]],
  kWasmFuncRef);

let inlinee_0 = builder.addFunction("inlinee_0", sigVoid).addBody([
  kExprGlobalGet, $global0.index,
  kExprTableGet, $table0.index,
  kGCPrefix, kExprRefCast, sigVoid,
  kExprCallRef, sigVoid,
]).exportFunc();

let inlinee_1 = builder.addFunction("inlinee_1", sigVoid).addBody([
  kExprCallFunction, inlinee_0.index,
]).exportFunc();

let inlinee_2 = builder.addFunction("inlinee_2", SigManyParams).addBody([
  kExprReturnCall, inlinee_1.index,
]).exportFunc();

let main = builder.addFunction("main", kSig_i_i).addBody([
    kExprLocalGet, 0,  // $var0
    kExprGlobalSet, $global0.index,
    kExprI32Const, 1,
    ...wasmI32Const(-1),
    ...wasmI32Const(1),
    ...wasmI32Const(1),
    ...wasmI32Const(1),
    ...wasmI32Const(1),
    ...wasmI32Const(1),
    kExprCallFunction, inlinee_2.index,
    ...wasmI32Const(214),
  ]).exportFunc();

const instance = builder.instantiate({});
let wasm = instance.exports;

assertEquals(214, wasm.main(0));
%WasmTierUpFunction(wasm.inlinee_2);
assertEquals(214, wasm.main(0));
assertEquals(214, wasm.main(1));
