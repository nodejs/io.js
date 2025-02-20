// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test case reproduces a DCHECK failure where outstanding_baseline_units_
// in the CompilationStateImpl was decremented even though it was already 0.
//
// These are the steps required to reproduce which are tested here:
// 1) Instantiate a wasm module, collect feedback and optimize a wasm function
//    inside it which includes a speculatively inlined call with a DeoptimizeIf
//    for the bailout.
// 2) Serialize the wasm module.
// 3) Deserialize the wasm module, create a new instance for it without using
//    the NativeModuleCache. This will mark the optimized function from (1) as
//    RequiredBaselineTier = kTurbofan.
// 4) Trigger the deoptimization on this deserialized Turbofan function. The
//    execution now goes back to Liftoff.
// 5) Continue running the Liftoff function until it triggers dynamic tier-up.
//    (It has to be a background compilation job!)
// 6) When the tier-up finishes, it sees that the compiled tier (Turbofan) is
//    the RequiredBaselineTier of that function while the reached tier is below
//    the RequiredBaselineTier (the liftoff code created during deoptimization).
//    It therefore decreases the outstanding_baseline_units_ for this instance.
//    (Note that this counter is meant to be relevant only during the
//    instantiation of a module (happening in step 3 above), afterwards it
//    should never be modified again.)
//    The code simply did not assume that we'd ever "drop below" the required
//    baseline tier and then repeat a compilation with the required baseline
//    tier, however with deopts this assumption doesn't hold true any more.
//    This leads to a DCHECK failure then as the outstanding_baseline_units_
//    should never be negative.

// Speculative inlining via deopts required:
// Flags: --wasm-inlining --wasm-deopt
// Dynamic tier-up required:
// Flags: --liftoff --wasm-dynamic-tiering
// The native module cache prevents hitting the bug:
// Flags: --no-wasm-native-module-cache
// Also reduce tiering budget to speed-up the busy wait on dynamic tier up:
// Flags: --wasm-tiering-budget=100 --expose-gc --allow-natives-syntax
// This test busy-waits for tier-up to be complete, hence it does not work in
// predictable more where we only have a single thread.
// Flags: --no-predictable

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptSerialized() {
  let wire_bytes = (function createWireBytes() {
    let builder = new WasmModuleBuilder();
    let funcRefT = builder.addType(kSig_i_ii);

    builder.addFunction("add", funcRefT)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
      .exportFunc();
    builder.addFunction("mul", funcRefT)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
      .exportFunc();
    builder.addFunction("sub", funcRefT)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Sub])
      .exportFunc();

    let mainSig =
      makeSig([kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);
    builder.addFunction("main", mainSig)
      .addBody([
        kExprI32Const, 12,
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kExprCallRef, funcRefT,
      ]).exportFunc();

    return builder.toBuffer();
  })();

  let module = new WebAssembly.Module(wire_bytes);
  { // Tier-up main function.
    let wasm = new WebAssembly.Instance(module).exports;
    assertEquals(42, wasm.main(30, wasm.add));
    %WasmTierUpFunction(wasm.main);
    assertEquals(42, wasm.main(30, wasm.add));
  }
  { // Replace module with deserialized variant.
    let buff = %SerializeWasmModule(module);
    module = null;
    gc(); gc();
    module = %DeserializeWasmModule(buff, wire_bytes);
  }
  {
    let wasm = new WebAssembly.Instance(module).exports;
    assertEquals(360, wasm.main(30, wasm.mul));
    // Trigger dynamic (background) tier-up.
    while (!%IsTurboFanFunction(wasm.main)) {
      assertEquals(360, wasm.main(30, wasm.mul));
    }
    // Trigger deopt again.
    assertEquals(-18, wasm.main(30, wasm.sub));
  }
})();
