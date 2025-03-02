// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --wasm-inlining-call-indirect
// Flags: --wasm-inlining-ignore-call-counts --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.startRecGroup();
let $array0 = builder.addArray(kWasmI8, true, kNoSuperType, true);
builder.endRecGroup();
let $sig5 = builder.addType(makeSig([], [kWasmI32]));
let callee_017 = builder.addFunction(undefined, $sig5);
let callee_118 = builder.addFunction(undefined, $sig5);
let main22 = builder.addFunction(undefined, kSig_i_i);
let $mem0 = builder.addMemory64(0, 32);
let $table0 = builder.addTable(kWasmFuncRef, 5, 5);
let $segment0 = builder.addActiveElementSegment($table0.index, wasmI32Const(0),
  [[kExprRefFunc, callee_017.index], [kExprRefFunc, callee_118.index]],
  kWasmFuncRef);

callee_017.addLocals(kWasmI32, 1).addBody([
  // This makes the return call in the main function polymorphic!
  kExprI32Const, 1,
  kExprCallFunction, main22.index,
  ...wasmI64Const(1n),
  ...wasmI64Const(2n),
  kSimdPrefix, kExprS128LoadMem, 1, ...wasmSignedLeb64(50629n),
  kExprDrop, kExprDrop,
]);

callee_118.addBody([
  ...wasmI32Const(-1),
]);

main22.addBody([
  kExprLocalGet, 0,
  kExprReturnCallIndirect, $sig5, $table0.index,
]);

builder.addExport('main', main22.index);

const instance = builder.instantiate({});
assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(0));
%WasmTierUpFunction(instance.exports.main);
assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(0));
