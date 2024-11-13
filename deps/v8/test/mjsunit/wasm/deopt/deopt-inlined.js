// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged
// Flags: --wasm-inlining-ignore-call-counts --no-jit-fuzzing
// Flags: --wasm-inlining-call-indirect

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptInlined() {
  let inlineeIndex = 2;
  let sig = 1;
  // These different tests define the call "kind" for the inlining of the
  // function called "inlinee", not the speculative inlining for add / mul.
  let tests = [
    {name: "callDirect", ops: [kExprCallFunction, inlineeIndex]},
    {name: "callRef", ops: [kExprRefFunc, inlineeIndex, kExprCallRef, sig]},
    {name: "callIndirect",
     ops: [kExprI32Const, 0, kExprCallIndirect, sig, kTableZero]},
    {name: "returnCall", ops: [kExprReturnCall, inlineeIndex]},
    {name: "returnCallRef",
     ops: [kExprRefFunc, inlineeIndex, kExprReturnCallRef, sig]},
    {name: "returnCallIndirect",
     ops: [kExprI32Const, 0, kExprReturnCallIndirect, sig, kTableZero]},
  ];

  for (let test of tests) {
    print(`test ${test.name}`);
    var builder = new WasmModuleBuilder();
    let funcRefT = builder.addType(kSig_i_ii);

    builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
    builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
    .exportFunc();

    let mainSig = builder.addType(makeSig([kWasmI32, kWasmI32,
      wasmRefType(funcRefT)], [kWasmI32]));
    assertEquals(sig, mainSig);
    let inlinee = builder.addFunction("inlinee", mainSig)
      .addBody([
        kExprLocalGet, 1,
        kExprI32Const, 1,
        kExprI32Add,
        kExprLocalTee, 1,
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kExprI32Add,
        kExprLocalTee, 0,
        kExprLocalGet, 2,
        kExprCallRef, funcRefT,
    ]).exportFunc();
    assertEquals(inlinee.index, inlineeIndex);

    builder.addFunction(test.name, mainSig)
      .addLocals(kWasmI32, 1)
      .addBody([
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kExprI32Add,
        kExprLocalGet, 1,
        kExprI32Const, 1,
        kExprI32Add,
        kExprLocalGet, 2,
        ...test.ops,
    ]).exportFunc();

    let table = builder.addTable(kWasmFuncRef, 1);
    builder.addActiveElementSegment(table.index, wasmI32Const(0),
      [[kExprRefFunc, inlinee.index]], kWasmFuncRef);

    let wasm = builder.instantiate().exports;

    let fct = wasm[test.name];
    assertEquals(46, fct(12, 30, wasm.add));
    // Tier up.
    %WasmTierUpFunction(fct);
    assertEquals(46, fct(12, 30, wasm.add));
    if (%IsWasmTieringPredictable()) {
      assertTrue(%IsTurboFanFunction(fct));
    }
    // Cause deopt.
    assertEquals(14 * 32, fct(12, 30, wasm.mul));
    // Deopt happened.
    if (%IsWasmTieringPredictable()) assertFalse(%IsTurboFanFunction(fct));
    assertEquals(46, fct(12, 30, wasm.add));
    // Trigger re-opt.
    %WasmTierUpFunction(fct);
    // Both call targets are used in the re-optimized function, so they don't
    // trigger new deopts.
    assertEquals(46, fct(12, 30, wasm.add));
    if (%IsWasmTieringPredictable()) {
      assertTrue(%IsTurboFanFunction(fct));
    }
    assertEquals(14 * 32, fct(12, 30, wasm.mul));
    if (%IsWasmTieringPredictable()) {
      assertTrue(%IsTurboFanFunction(fct));
    }
  }
})();
