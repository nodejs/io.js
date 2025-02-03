// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff --no-jit-fuzzing
// Flags: --turboshaft-wasm-instruction-selection-staged
// Flags: --wasm-inlining-ignore-call-counts --wasm-inlining-factor=15

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// This test case covers a bug where a return call would result in not adding
// the parent FrameState to the deopt node. It is correct that the frame with
// the return call should be skipped (due to the nature of tail calls), however,
// any inlined parent frames of the return call (in this case the main function)
// need to be included.
//
// So, in this particular case at the deopt there should be a FrameState for:
// - inlinee1 (just prior to the callRef)
// - main (just behind the call) as a parent of inlinee1's FrameState
// But no FrameState for inlinee2.
(function TestDeoptInlinedReturnCall() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_v_v);

  builder.addFunction("c1", funcRefT).addBody([]).exportFunc();
  builder.addFunction("c2", funcRefT).addBody([]).exportFunc();

  let inlineeSig = makeSig([wasmRefType(funcRefT)], []);
  let inlinee1 = builder.addFunction("inlinee1", inlineeSig).addBody([
    kExprLocalGet, 0,
    kExprCallRef, funcRefT, // the deopt point.
  ]);
  let inlinee2 = builder.addFunction("inlinee2", inlineeSig).addBody([
    kExprLocalGet, 0,
    kExprReturnCall, inlinee1.index,
  ]);

  let mainSig =
    makeSig([wasmRefType(funcRefT)], [kWasmI32]);
  builder.addFunction("main", mainSig)
    .addLocals(kWasmI32, 1)
    .addBody([
      ...wasmI32Const(1234567),
      kExprLocalSet, 1,
      kExprLocalGet, 0,
      kExprCallFunction, inlinee2.index,
      kExprLocalGet, 1,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(1234567, wasm.main(wasm.c1));
  %WasmTierUpFunction(wasm.main);
  assertEquals(1234567, wasm.main(wasm.c2));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
})();
