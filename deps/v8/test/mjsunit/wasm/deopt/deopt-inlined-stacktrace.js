// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged
// Flags: --wasm-inlining-ignore-call-counts --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptInlinedStacktrace() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("div", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32DivS])
    .exportFunc();

  let mainSig =
    makeSig([kWasmI32, kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);
  let inlinee = builder.addFunction("inlinee", mainSig)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  builder.addFunction("main", mainSig)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallFunction, inlinee.index,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  // Collect feedback.
  assertEquals(42, wasm.main(12, 30, wasm.add));
  // Trigger tierup.
  %WasmTierUpFunction(wasm.main);
  assertEquals(42, wasm.main(12, 30, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  // Trigger deopt which then calls a target that throws, i.e. the stack trace
  // contains frames created by the deoptimizer.
  try {
    wasm.main(10, 0, wasm.div);
    assertUnreachable();
  } catch (error) {
    verifyException(error);
  }
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  // Rerun unoptimized which should produce the same result.
  try {
    wasm.main(10, 0, wasm.div);
    assertUnreachable();
  } catch (error) {
    verifyException(error);
  }
  %WasmTierUpFunction(wasm.main);
  // Rerun optimized which should also produce the same result and not deopt.
  try {
    wasm.main(10, 0, wasm.div);
    assertUnreachable();
  } catch (error) {
    verifyException(error);
  }
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }

  function verifyException(error) {
    assertMatches(/RuntimeError: divide by zero/, error + "");
    let stack = error.stack.split("\n");
    let expected = [
      /RuntimeError: divide by zero/,
      /at div .*wasm-function\[1\]:0x59/,
      /at inlinee .*wasm-function\[2\]:0x63/,
      /at main .*wasm-function\[3\]:0x6e/,
    ];
    for (let i = 0; i < expected.length; ++i) {
      assertMatches(expected[i], stack[i]);
    }
  }
})();
