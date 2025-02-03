// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-wasm
// Flags: --wasm-inlining-call-indirect --wasm-inlining-ignore-call-counts

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestUnreachableCallIndirect() {
  var builder = new WasmModuleBuilder();

  // Growable memory causes the Turboshaft graph builder to cache the memory
  // size.
  builder.addMemory(1, 2);
  let funcRefT = builder.addType(kSig_i_ii);

  let add = builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();

  // A function which "terminates" the control flow when inlined.
  let unreachable = builder.addFunction("unreachable", kSig_v_v).addBody([
    kExprUnreachable,
  ]);

  let table = builder.addTable(kWasmFuncRef, 1);
  builder.addActiveElementSegment(table.index, wasmI32Const(0), [
    [kExprRefFunc, add.index],
  ], kWasmFuncRef);

  let mainSig =
    makeSig([kWasmI32], [kWasmI32]);
  let main = builder.addFunction("main", mainSig);
  main.addBody([
      kExprI32Const, 30,
      kExprI32Const, 12,
      kExprLocalGet, 0, // target index
      kExprCallIndirect, funcRefT, table.index,

      kExprI32Const, 0,
      kExprIf, kWasmVoid,
        // Inlining this function causes the graph builder to be in the
        // __ generating_unreachable_operations() state.
        kExprCallFunction, unreachable.index,
        kExprI32Const, 0,
        // Recursive call. Note that when this gets inlined, we can also inline
        // the call_indirect above as it does have feedback collected.
        kExprReturnCall, main.index,
      kExprEnd,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(42, wasm.main(0));
  %WasmTierUpFunction(wasm.main);
  assertEquals(42, wasm.main(0));
 })();
