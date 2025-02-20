// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This tests that when speculatively inlining tail calls, we make sure to
// re-load cached fields from the instance _after_ all the different arms in
// case of nested inlining. (We need nested inlining for code to appear after a
// tail call, since otherwise the tail call terminates control-flow.)

// Flags: --allow-natives-syntax
// Flags: --wasm-inlining-ignore-call-counts
// For the subtest involving `return_call_indirect`.
// Flags: --wasm-inlining-call-indirect
// We enable explicit bounds checks so that memory accesses load the memory
// size from the `InstanceCache`.
// Flags: --wasm-enforce-bounds-checks

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function Test(callType, whatToInline) {
  console.log(`Test: ${callType}, ${whatToInline}`);

  const builder = new WasmModuleBuilder();

  let sig_index = builder.addType(kSig_v_v);
  let mem_index = builder.addMemory(0);

  let mutateInstanceField = builder.addFunction(undefined, kSig_v_v).addBody([
    ...wasmI32Const(1),
    kExprMemoryGrow, mem_index,
    kExprDrop,
  ]);
  let noMutateInstanceField = builder.addFunction(undefined, kSig_v_v).addBody([
    kExprNop,
  ]);

  let table = builder.addTable(kWasmFuncRef, 2, 2);
  builder.addActiveElementSegment(table.index, wasmI32Const(0), [
    [kExprRefFunc, mutateInstanceField.index],
    [kExprRefFunc, noMutateInstanceField.index],
  ], kWasmFuncRef);
  const kMutateInstanceFieldTableIndex = 0;
  const kNoMutateInstanceFieldTableIndex = 1;

  let inlinedInBetween = builder.addFunction(undefined, kSig_v_i);
  if (callType === 'return_call_ref') {
    inlinedInBetween.addBody([
      kExprLocalGet, /* parameter */ 0,
      kExprTableGet, table.index,
      kGCPrefix, kExprRefCast, sig_index,
      kExprReturnCallRef, sig_index
    ]);
  } else if (callType === 'return_call_indirect') {
    inlinedInBetween.addBody([
      kExprLocalGet, /* parameter */ 0,
      kExprCallIndirect, sig_index, table.index
    ]);
  }

  builder.addFunction("main", kSig_i_i).addBody([
    kExprLocalGet, 0,
    kExprCallFunction, inlinedInBetween.index,
    // Uses the mutable `memory_size_` field from the `InstanceCache` due to
    // --wasm-enforce-bounds-checks.
    ...wasmI32Const(42),
    kExprI32LoadMem, mem_index, /* no alignment hint */ 0,
  ]).exportFunc();

  let { main } = builder.instantiate({}).exports;
  if (whatToInline === "inline_memory_grow") {
    // 1. Collect feedback with the version we want to be inlined.
    // 2. Compile optimized code, transitively inline (confirm with --trace-wasm-inlining).
    // 3. Run Turboshaft compiled version, but now run the slowpath.
    assertEquals(0, main(kMutateInstanceFieldTableIndex));
    %WasmTierUpFunction(main);
    assertEquals(0, main(kNoMutateInstanceFieldTableIndex));
  } else if (whatToInline === "inline_nop") {
    // Same as above, just that what is the inlined vs. slowpath is reversed.
    assertTraps(kTrapMemOutOfBounds, () => main(kNoMutateInstanceFieldTableIndex));
    %WasmTierUpFunction(main);
    assertEquals(0, main(kMutateInstanceFieldTableIndex));
  }
}

Test("return_call_ref", "inline_memory_grow");
Test("return_call_ref", "inline_nop");
Test("return_call_indirect", "inline_memory_grow");
Test("return_call_indirect", "inline_nop");
