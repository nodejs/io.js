// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax
// Flags: --wasm-inlining-call-indirect --wasm-inlining-ignore-call-counts

// Same-module but different-instance calls should be handled correctly.
// Wasm functions are implicit closures of the instance. When inlining code from
// a callee into a caller, they both implicitly use the same instance in our
// codegen. We thus may not execute the inlined code if the call target happens
// to use a different instance (even if it's a function in the same module and
// not imported).

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
  builder
    .addFunction("return_call_indirect", type_i_v)
    .addBody([
      ...wasmI32Const(/* index of funcref in table */ 0),
      kExprReturnCallIndirect, type_i_v, kTableZero,
    ])
    .exportFunc();
  builder
    .addFunction("call_ref", type_i_v)
    .addBody([
      ...wasmI32Const(/* index of funcref in table */ 0),
      // This is essentially the same as for call_indirect above, just desugared.
      kExprTableGet, kTableZero,
      kGCPrefix, kExprRefCast, type_i_v,
      kExprCallRef, type_i_v,
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

function test(callType) {
  print("test " + callType);

  // A function from the second instance is invoked, which then indirectly
  // calls `callee` from the first instance, which should semantically close
  // over (in the sense of "closure") the global with value 1000.
  const f = instance2.exports[callType];
  f();
  %WasmTierUpFunction(f);
  assertEquals(1005, f());
}
test("call_indirect");
test("return_call_indirect");
test("call_ref");
