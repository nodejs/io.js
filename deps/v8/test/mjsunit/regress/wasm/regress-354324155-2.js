// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-wasm --wasm-inlining-call-indirect

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestUnreachableCallIndirect() {
  var builder = new WasmModuleBuilder();

  // Growable memory causes the Turboshaft graph builder to cache the memory
  // size.
  builder.addMemory(1, 2);
  let funcRefT = builder.addType(makeSig([], [kWasmI32]));

  let callee = builder.addFunction("42", funcRefT)
    .addBody([kExprI32Const, 42]).exportFunc();

  // A function which "terminates" the control flow when inlined.
  let unreachable = builder.addFunction("unreachable", kSig_v_v).addBody([
    kExprUnreachable,
  ]);

  let table = builder.addTable(kWasmFuncRef, 1);
  builder.addActiveElementSegment(table.index, wasmI32Const(0), [
    [kExprRefFunc, callee.index],
  ], kWasmFuncRef);

  let performCall = builder.addFunction("performCall", kSig_i_i).addBody([
    kExprLocalGet, 0,
    kExprCallIndirect, funcRefT, table.index,
  ]).exportFunc();

  let main = builder.addFunction("main", kSig_i_i);
  main.addBody([
      kExprI32Const, 0,
      kExprIf, kWasmVoid,
        // Inlining this function causes the graph builder to be in the
        // __ generating_unreachable_operations() state.
        kExprCallFunction, unreachable.index,
        kExprI32Const, 0,
        // Call to function with call_indirect. Note that when this gets
        // inlined, we can also inline the call_indirect above as it does have
        // feedback collected.
        // Note that this only gets inlined because it's small enough that we
        // inline it independently of call counts.
        kExprReturnCall, performCall.index,
      kExprEnd,
      kExprI32Const, 11,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  for (let i = 0; i < 30; ++i) {
    assertEquals(42, wasm.performCall(0));
  }
  %WasmTierUpFunction(wasm.performCall);
  assertEquals(11, wasm.main(0));
  %WasmTierUpFunction(wasm.main);
  assertEquals(11, wasm.main(0));
 })();
