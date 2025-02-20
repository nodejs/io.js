// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-staging

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

let $glob0 =
    builder.addImportedGlobal('imports', 'glob0', wasmRefNullType(kWasmExnRef));
let $glob1 = builder.addGlobal(kWasmExnRef);
let $glob2 = builder.addGlobal(kWasmNullExnRef);
let $tab1 = builder.addTable(kWasmExnRef, 0, 1).exportAs("tab1");
let $tab2 = builder.addTable(kWasmNullExnRef, 0, 1).exportAs("tab2");

// To allow the tierup-triggering loop below to handle all exported functions
// the same way, they all stick to the contract that they trap with
// "unreachable" on success (!). If they terminate without trapping, the
// test fails.

builder.addFunction("test_glob0", kSig_v_v).exportFunc().addBody([
  kExprGlobalGet, $glob0,
  kExprBrOnNonNull, 0,
  kExprUnreachable,
]);
builder.addFunction("test_glob1", kSig_v_v).exportFunc().addBody([
  kExprGlobalGet, $glob1.index,
  kExprBrOnNonNull, 0,
  kExprUnreachable,
]);
builder.addFunction("test_glob2", kSig_v_v).exportFunc().addBody([
  kExprGlobalGet, $glob2.index,
  kExprBrOnNonNull, 0,
  kExprUnreachable,
]);
builder.addFunction("test_tab1", kSig_v_v).exportFunc().addBody([
  kExprI32Const, 0,
  kExprTableGet, $tab1.index,
  kExprBrOnNonNull, 0,
  kExprUnreachable,
]);
builder.addFunction("test_tab2", kSig_v_v).exportFunc().addBody([
  kExprI32Const, 0,
  kExprTableGet, $tab2.index,
  kExprBrOnNonNull, 0,
  kExprUnreachable,
]);

// This signature isn't JS-compatible, so we have to call the function
// from another Wasm function.
let sig_uninhabitable = makeSig([], [wasmRefType(kWasmNullExnRef)]);
let crash_callee = builder.addFunction("crash_callee", sig_uninhabitable)
  .addBody([
    kExprBlock, kWasmVoid,
      kExprI32Const, 0,
      kExprTableGet, $tab2.index,
      kExprBrOnNull, 0,
      kExprReturn,
    kExprEnd,
    kExprUnreachable,
  ]);
builder.addFunction("crash", kSig_v_v).exportFunc().addBody([
  kExprCallFunction, crash_callee.index,
  kExprDrop,
])

let glob0 = new WebAssembly.Global({value: 'exnref'});
// Not (yet?) supported.
// TODO(thibaudm): Figure out whether this should be supported, and either
// drop it or add it to the set of cases being tested.
assertThrows(() => {
  let tab0 = new WebAssembly.Table({element: 'exnref', initial: 1, maximum: 2});
}, TypeError);

let imports = { glob0 };
let instance = builder.instantiate({ imports });
let wasm = instance.exports;
wasm.tab1.grow(1);
wasm.tab2.grow(1);
for (let funcname in wasm) {
  let func = wasm[funcname];
  if (typeof func !== "function") continue;
  console.log(funcname);
  assertThrows(func, WebAssembly.RuntimeError, "unreachable");
  %WasmTierUpFunction(func);
  assertThrows(func, WebAssembly.RuntimeError, "unreachable");
}
