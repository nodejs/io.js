// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --no-jit-fuzzing
// Flags: --wasm-inlining-ignore-call-counts --liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig5 = builder.addType(kSig_v_v);
let $sig9 = builder.addType(kSig_i_i);
let callee_017 = builder.addFunction("callee0", $sig5).addBody([]);
let callee_118 = builder.addFunction("callee1", $sig5).addBody([]);
let $global0 = builder.addGlobal(kWasmI32, true, false, wasmI32Const(0));
let $table0 = builder.addTable(kWasmFuncRef, 8, 8);
let $segment0 = builder.addActiveElementSegment($table0.index, wasmI32Const(0),
    [[kExprRefFunc, callee_017.index], [kExprRefFunc, callee_118.index]],
    kWasmFuncRef);
let $tag0 = builder.addTag($sig5);

builder.addFunction("main", $sig9).exportFunc().addBody([
  kExprTry, kWasmVoid,
    kExprThrow, $tag0,
  kExprCatch, $tag0,
    kExprLocalGet, 0,  // $var0
    kExprTableGet, $table0.index,
    kGCPrefix, kExprRefCast, $sig5,
    kExprCallRef, $sig5,
  kExprEnd,
  kExprI32Const, 42,
]);

builder.addFunction("mainNested", $sig9).exportFunc().addBody([
  kExprI32Const, 42,
  kExprTry, kWasmVoid,
    kExprThrow, $tag0,
  kExprCatchAll,
    kExprI32Const, 0,
    kExprTry, kWasmVoid,
      kExprThrow, $tag0,
    kExprCatchAll,
      kExprLocalGet, 0,  // $var0
      kExprTableGet, $table0.index,
      kGCPrefix, kExprRefCast, $sig5,
      kExprCallRef, $sig5,
    kExprEnd,
    kExprDrop,
  kExprEnd,
]);

const instance = builder.instantiate({});
const wasm = instance.exports;

assertEquals(42, wasm.mainNested(0));
%WasmTierUpFunction(wasm.mainNested);
assertEquals(42, wasm.mainNested(1));

assertEquals(42, wasm.main(0));
%WasmTierUpFunction(wasm.main);
assertEquals(42, wasm.main(1));
