// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging
// Flags: --allow-natives-syntax --wasm-inlining-ignore-call-counts
// The following flags are not needed but simplify the graph while still
// reproducing the bug:
// --wasm-inlining-factor=1 --nodebug-code

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestSpeculativeInliningInstanceCacheReturnCallRef() {
  const builder = new WasmModuleBuilder();
  let $struct2 =
    builder.addStruct([makeField(kWasmI32, false)], kNoSuperType, false);
  builder.startRecGroup();
  let $array4 = builder.addArray(kWasmI32, true, kNoSuperType, false);
  let $sig5 = builder.addType(makeSig([], [kWasmI32]));
  builder.endRecGroup();
  let callee_017 = builder.addFunction(undefined, $sig5);
  let callee_118 = builder.addFunction(undefined, $sig5);
  let main22 = builder.addFunction(undefined, kSig_i_i);
  let $mem0 = builder.addMemory(0, 32);
  let $global0 = builder.addGlobal(kWasmI32, true, false, wasmI32Const(0));
  let $table0 = builder.addTable(wasmRefType($sig5), 2, 2,
    [kExprRefFunc, callee_017.index]);
  builder.addActiveElementSegment($table0.index, wasmI32Const(0),
    [[kExprRefFunc, callee_017.index], [kExprRefFunc, callee_118.index]],
    wasmRefType($sig5));

  callee_017.addBody([
    kExprI32Const, 1,
    kGCPrefix, kExprStructNew, $struct2,
    kGCPrefix, kExprStructGet, $struct2, 0,
    kExprCallFunction, main22.index,
  ]);

  callee_118.addBody([
    kExprI32Const, 0,
    kExprI32Const, 0,
    kGCPrefix, kExprArrayNewDefault, $array4,
    kExprMemorySize, $mem0,
    kGCPrefix, kExprArrayGet, $array4,
    kExprI32DivS,
  ]);

  // func $main: [kWasmI32] -> [kWasmI32]
  main22.addBody([
    kExprLocalGet, 0,  // $var0
    kExprGlobalSet, $global0.index,
    kExprGlobalGet, $global0.index,
    kExprTableGet, $table0.index,
    kGCPrefix, kExprRefCast, $sig5,
    kExprReturnCallRef, $sig5,
  ]);

  builder.addExport('main', main22.index);

  const instance = builder.instantiate({});
  assertTraps(kTrapArrayOutOfBounds, () => instance.exports.main(0));
  %WasmTierUpFunction(instance.exports.main);
  assertTraps(kTrapArrayOutOfBounds, () => instance.exports.main(0));
})();
