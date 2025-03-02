// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-deopt --liftoff
// Flags: --wasm-inlining-call-indirect --wasm-inlining-ignore-call-counts

// See also inlining-multi-instance.js, from which this is adapted.
// This additionally checks that we don't run into a deopt loop.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function setup() {
  const kInitialTableSize = 1;
  let sharedTable = new WebAssembly.Table({
    element: "anyfunc",
    initial: kInitialTableSize,
  });

  let builder = new WasmModuleBuilder();
  let type_i_v = builder.addType(kSig_i_v);
  builder.addImportedTable("env", "table", kInitialTableSize);
  // We use this global's value to differentiate which instance we are executing.
  let importedGlobal = builder.addImportedGlobal("env", "g", kWasmI32);
  builder
    .addFunction("callee", kSig_i_v)
    .addBody([kExprGlobalGet, importedGlobal, ...wasmI32Const(5), kExprI32Add])
    .exportFunc();
    builder
    .addFunction("call_indirect", type_i_v)
    .addBody([
      ...wasmI32Const(/* index of funcref in table */ 0),
      kExprCallIndirect, type_i_v, kTableZero,
    ])
    .exportFunc();
  let wasmModule = builder.toModule();

  // Both instances share the same table, but have different global values to
  // distinguish them.
  let instance1 = new WebAssembly.Instance(wasmModule, {
    env: {
      g: 1000,
      table: sharedTable,
    },
  });
  let instance2 = new WebAssembly.Instance(wasmModule, {
    env: {
      g: 2000,
      table: sharedTable,
    },
  });

  // And the table contains a reference to the function of the first instance.
  sharedTable.set(0, instance1.exports.callee);
  return {instance1, instance2};
}
let {instance1, instance2} = setup();

const func1 = instance1.exports.call_indirect;
const func2 = instance2.exports.call_indirect;

// Same-instance call can be inlined.
assertEquals(1005, func1());
%WasmTierUpFunction(func1);
if (%IsWasmTieringPredictable()) {
  assertTrue(%IsTurboFanFunction(func1));
  // Since we share code between instances, this is optimized as well.
  assertTrue(%IsTurboFanFunction(func2));
}

// Cross-instance call triggers a deopt (and returns the correct result).
assertEquals(1005, func2());
if (%IsWasmTieringPredictable()) {
  assertFalse(%IsTurboFanFunction(func1));
  assertFalse(%IsTurboFanFunction(func2));
}

// Re-optimizing after deopting due to non-inlineable target, we may not deopt
// again, otherwise we would have a deopt loop.
%WasmTierUpFunction(func2);
assertEquals(1005, func1());
assertEquals(1005, func2());
if (%IsWasmTieringPredictable()) {
  assertTrue(%IsTurboFanFunction(func1));
  assertTrue(%IsTurboFanFunction(func2));
}
