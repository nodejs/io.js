// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-deopt --liftoff --wasm-inlining
// Flags: --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let createUniqueModule = (function() {
  // By using a different amount of locals on each module, they are going to be
  // unique, so different test cases don't interfer with each other.
  let localsCount = 0;
  return function createModule() {
    var builder = new WasmModuleBuilder();
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
    builder.addFunction("first", funcRefT)
      .addBody([kExprLocalGet, 0])
      .exportFunc();
    builder.addFunction("second", funcRefT)
      .addBody([kExprLocalGet, 1])
      .exportFunc();
    builder.addFunction("equals", funcRefT)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Eq,])
      .exportFunc();

    let mainSig =
      makeSig([kWasmI32, kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);
    builder.addFunction("main", mainSig)
      .addLocals(kWasmI32, ++localsCount)
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kExprLocalGet, 2,
        kExprCallRef, funcRefT,
        kExprLocalTee, 3,
        // If the result is 0, repeat the call, so that we have a second deopt
        // point.
        kExprI32Eqz,
        kExprIf, kWasmVoid,
          kExprI32Const, 0,
          kExprI32Const, 0,
          kExprLocalGet, 2,
          kExprCallRef, funcRefT,
          kExprDrop,
        kExprEnd,
        kExprLocalGet, 3,
    ]).exportFunc();
    return builder.toModule();
  }
})();

// Test a scenario where each instance only has polymorphic call feedback but
// the instsances combined sum up to more feedback slots than the maximum
// polymorphism (4).
(function TestCollectiveFeedbackMoreThanMaxPolymorphism() {
  let module = createUniqueModule();

  let wasm1 = new WebAssembly.Instance(module).exports;
  // Collect polymorphic feedback for wasm1.
  assertEquals(42, wasm1.main(30, 12, wasm1.add));
  assertEquals(360, wasm1.main(30, 12, wasm1.mul));
  assertEquals(18, wasm1.main(30, 12, wasm1.sub));
  %WasmTierUpFunction(wasm1.main);
  // Trigger deopt and collect polymorphic feedback for wasm2.
  let wasm2 = new WebAssembly.Instance(module).exports;
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm2.main));
  }
  assertEquals(30, wasm2.main(30, 12, wasm2.first));
  if (%IsWasmTieringPredictable()) {
    // Deopt due to inlined target mismatch.
    assertFalse(%IsTurboFanFunction(wasm2.main));
  }
  // Returning zero here means that the second call_ref in the main function now
  // has feedback for "second".
  assertEquals(0, wasm2.main(30, 0, wasm2.second));

  // We have 5 different call targets, so the call is megamorphic. (The
  // per-instance feedback for both instances is polymorphic.)
  %WasmTierUpFunction(wasm2.main);
  for (let wasm of [wasm1, wasm2]) {
    assertEquals(42, wasm.main(30, 12, wasm.add));
    assertEquals(360, wasm.main(30, 12, wasm.mul));
    assertEquals(18, wasm.main(30, 12, wasm.sub));
    assertEquals(30, wasm.main(30, 12, wasm.first));
    assertEquals(12, wasm.main(30, 12, wasm.second));
    // New targets don't trigger a de-opt either (as long as they don't return
    // zero which would cause them executing the second call_ref).
    assertEquals(1, wasm.main(30, 30, wasm.equals));
    if (%IsWasmTieringPredictable()) {
      assertTrue(%IsTurboFanFunction(wasm.main));
    }
  }

  let wasm3 = new WebAssembly.Instance(module).exports;
  assertEquals(42, wasm3.main(30, 12, wasm3.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm3.main));
  }
  // Trigger deoptimization on second call_ref which so far has only seen the
  // "second" function.
  assertEquals(0, wasm3.main(12, 12, wasm3.sub));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm3.main));
  }
  // Now that we are back in liftoff, collect feedback for equals on the first
  // call_ref as well.
  assertEquals(0, wasm3.main(30, 12, wasm3.equals));
  %WasmTierUpFunction(wasm3.main);

  // The first call is still megamorphic, so we can call all functions without
  // triggering a deopt (as long as the result isn't zero).
  for (let wasm of [wasm1, wasm2, wasm3]) {
    assertEquals(42, wasm.main(30, 12, wasm.add));
    assertEquals(360, wasm.main(30, 12, wasm.mul));
    assertEquals(18, wasm.main(30, 12, wasm.sub));
    assertEquals(30, wasm.main(30, 12, wasm.first));
    assertEquals(12, wasm.main(30, 12, wasm.second));
    assertEquals(1, wasm.main(30, 30, wasm.equals));
    if (%IsWasmTieringPredictable()) {
      assertTrue(%IsTurboFanFunction(wasm.main));
    }
  }
})();

// Test a scenario where the processed feedback is megamorphic based on the
// first module (using the megamorphic symbol in the feedback vector) and we get
// inlineable (in this case monomorphic but would be the same for polymorphic)
// feedback from the second module triggering a tier-up.
// The new tier-up should not overwrite / replace the megamorphic feedback.
(function TestMegamorphicFeedbackStaysMegamorphic() {
  let module = createUniqueModule();

  let wasm1 = new WebAssembly.Instance(module).exports;
  // Collect megamorphic feedback for the first call_ref on wasm1.
  assertEquals(42, wasm1.main(30, 12, wasm1.add));
  assertEquals(360, wasm1.main(30, 12, wasm1.mul));
  assertEquals(18, wasm1.main(30, 12, wasm1.sub));
  assertEquals(30, wasm1.main(30, 12, wasm1.first));
  assertEquals(0, wasm1.main(30, 0, wasm1.second));
  %WasmTierUpFunction(wasm1.main);
  // As the feedback is megamorphic, new call targets don't trigger a deopt.
  assertEquals(1, wasm1.main(30, 30, wasm1.equals));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm1.main));
  }

  // Triggering a deopt with the first instance. (Note that for the second
  // call_ref we have speculative inlining for wasm.second, so we can trigger a
  // deopt on that call_ref.)
  assertEquals(0, wasm1.main(0, 0, wasm1.add)); // MARKER_FOR_BELOW
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm1.main));
  }

  // Create a second instance. Initialize its feedback with a single target for
  // both call_refs (sub).
  let wasm2 = new WebAssembly.Instance(module).exports;
  assertEquals(0, wasm2.main(30, 30, wasm2.sub));
  // Tiering up with the feedback from the second instance, the merged feedback
  // has to be megamorphic for the first call_ref.
  %WasmTierUpFunction(wasm2.main);
  for (let wasm of [wasm1, wasm2]) {
    assertEquals(42, wasm.main(30, 12, wasm.add));
    assertEquals(360, wasm.main(30, 12, wasm.mul));
    assertEquals(18, wasm.main(30, 12, wasm.sub));
    assertEquals(30, wasm.main(30, 12, wasm.first));
    assertEquals(12, wasm.main(30, 12, wasm.second));
    assertEquals(1, wasm.main(30, 30, wasm.equals));
    if (%IsWasmTieringPredictable()) {
      assertTrue(%IsTurboFanFunction(wasm.main));
    }
  }

  // Just highlighting a shortcoming of the current implementation, the feedback
  // for the second call_ref with wasm.add as a call target that triggered the
  // deopt above can still trigger a deopt as we did not tier up with that
  // feedback, yet, so the collected processed feedback doesn't know about it.
  assertEquals(0, wasm2.main(0, 0, wasm2.add));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm1.main));
    assertFalse(%IsTurboFanFunction(wasm2.main));
  }
  // Now, if we trigger another tierup on wasm1, the feedback for the add call
  // target for the second call_ref will be used. Note that this feedback was
  // collected at the line with the "MARKER_FOR_BELOW" comment already, the
  // deopt just above only collects feedback on the executing instance wasm2.
  %WasmTierUpFunction(wasm1.main);
  // No more deopt for add on the second call_ref.
  assertEquals(0, wasm1.main(0, 0, wasm1.add));
  assertEquals(0, wasm2.main(0, 0, wasm2.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm1.main));
    assertTrue(%IsTurboFanFunction(wasm2.main));
  }
})();
