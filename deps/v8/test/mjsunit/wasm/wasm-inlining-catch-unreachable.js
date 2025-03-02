// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-staging

// This test case reproduces an issue found in crbug.com/1508213 where
// reachability is handled differently for unreachable catch blocks in liftoff
// and Turboshaft causing Turboshaft to assign wrong feedback slots for calls.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function InliningDecisionsWithUnreachableCatchBlocks() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  // Make it a wasm-gc module to enable inlining.
  builder.addArray(kWasmI32, true);

  let except = builder.addTag(makeSig([], []));

  let calleeVoid = builder.addFunction("calleeVoid", makeSig([], []))
  .addBody([])
  .exportFunc();

  builder.addFunction("id", makeSig([kWasmI32], [kWasmI32]))
  .addBody([kExprLocalGet, 0])
  .exportFunc();

  let i32_sig = builder.addType(makeSig([kWasmI32], [kWasmI32]));
  let caller_sig = makeSig([wasmRefType(i32_sig)], [kWasmI32]);

  let testCases = [
    {name: "CatchAll",
     tryCatchCode: [
      kExprTry, kWasmVoid,
      kExprCatchAll,
        // This block is "dynamically unreachable".
        // Liftoff will not allocate a feedback slot for it.
        kExprCallFunction, calleeVoid.index,
      kExprEnd,
    ]},
    {name: "CatchException",
     tryCatchCode: [
      kExprTry, kWasmVoid,
      kExprCatch, except,
        // This block is "dynamically unreachable".
        // Liftoff will not allocate a feedback slot for it.
        kExprCallFunction, calleeVoid.index,
      kExprEnd,
    ]},
    {name: "CatchNoRef",
     tryCatchCode: [
      kExprBlock, kWasmVoid,
        kExprTryTable, kWasmVoid, 1,
        kCatchNoRef, except, 0,
          kExprBr, 1,
        kExprEnd,
        // This block is "dynamically unreachable".
        // Liftoff will not allocate a feedback slot for it.
        kExprCallFunction, calleeVoid.index,
      kExprEnd,
    ]},
  ];

  for (let testCase of testCases) {
    builder.addFunction(`caller${testCase.name}`, caller_sig)
      .addBody([
        ...testCase.tryCatchCode,
        kExprI32Const, 42,
        kExprLocalGet, 0,
        // We hit the correct feedback slot for inlining only if the unreachable
        // calls above are handled consistently between Liftoff and Turboshaft.
        kExprCallRef, i32_sig,
        kExprCallFunction, calleeVoid.index,
      ])
      .exportFunc();
  }

  let wasm = builder.instantiate().exports;

  for (let testCase of testCases) {
    print(`Test ${testCase.name}`);
    let wasmFct = wasm[`caller${testCase.name}`];
    // Collect feedback.
    for (let i = 0; i < 100; ++i) {
      assertEquals(42, wasmFct(wasm.id));
    }
    // Compile with Turboshaft and run again.
    %WasmTierUpFunction(wasmFct);
    assertEquals(42, wasmFct(wasm.id));
  }
})();
