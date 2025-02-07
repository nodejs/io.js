// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-deopt --wasm-inlining-ignore-call-counts
// Flags: --liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig8 = builder.addType(makeSig([], [wasmRefNullType(kWasmEqRef)]));
let $sig11 = builder.addType(makeSig([kWasmI64], []));
let $sig12 = builder.addType(kSig_i_i);
let callee_017 = builder.addFunction(undefined, $sig8);
let callee_118 = builder.addFunction(undefined, $sig8);
let inlinee_224 = builder.addFunction("inlinee_2", $sig11).exportFunc();
let main25 = builder.addFunction("main", $sig12).exportFunc();
let $global0 = builder.addGlobal(kWasmI32, true, false, wasmI32Const(0));
let $table0 = builder.addTable(kWasmFuncRef, 8, 8);
let $segment0 = builder.addActiveElementSegment($table0.index, wasmI32Const(0),
  [[kExprRefFunc, callee_017.index], [kExprRefFunc, callee_118.index],
   [kExprRefFunc, inlinee_224.index]],
  kWasmFuncRef);

callee_017.addBody([
    kExprUnreachable
  ]);

callee_118.addBody([
    kExprUnreachable,
  ]);

inlinee_224.addBody([
    kExprGlobalGet, $global0.index,
    kExprTableGet, $table0.index,
    kGCPrefix, kExprRefCast, $sig8,
    kExprCallRef, $sig8,
    kExprUnreachable,
  ]);

main25.addLocals(wasmRefNullType($sig12), 1).addBody([
    kExprLocalGet, 0,
    kExprGlobalSet, $global0.index,
    ...wasmI64Const(1n),
    kExprCallFunction, inlinee_224.index,
    kExprUnreachable,
  ]);

const instance = builder.instantiate({});
assertThrows(() => instance.exports.main(0));
%WasmTierUpFunction(instance.exports.inlinee_2);
assertThrows(() => instance.exports.main(1));
%WasmTierUpFunction(instance.exports.inlinee_2);
