// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-exnref

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let kSig_e_v = makeSig([], [kWasmExnRef]);

// First we just test that "exnref" global variables are allowed.
(function TestGlobalExnRefSupported() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let g = builder.addGlobal(kWasmExnRef, false, false);
  builder.addFunction("push_and_drop_exnref", kSig_v_v)
      .addBody([
        kExprGlobalGet, g.index,
        kExprDrop,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertDoesNotThrow(instance.exports.push_and_drop_exnref);
})();

// Test default value that global "exnref" variables are initialized with.
(function TestGlobalExnRefDefaultValue() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let g = builder.addGlobal(kWasmExnRef, false, false);
  builder.addFunction('push_and_check_exnref', kSig_i_v)
      .addBody([
          kExprGlobalGet, g.index,
          kExprRefIsNull, kExnRefCode,
      ])
      .exportFunc();
  let instance = builder.instantiate();

  assertEquals(1, instance.exports.push_and_check_exnref());
})();

// Test custom initialization index for a global "exnref" variable.
(function TestGlobalExnRefInitIndex() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let g_index = builder.addImportedGlobal("m", "exn", kWasmExnRef);
  builder.addFunction('push_and_return_exnref', kSig_e_v)
      .addBody([kExprGlobalGet, g_index])
      .exportFunc();
  assertThrows(() => builder.instantiate({ "m": { "exn": {} }}), WebAssembly.LinkError);
  assertThrows(() => builder.instantiate({ "m": { "exn": null }}), WebAssembly.LinkError);
})();

(function TestGlobalExnRefJsApi() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let g_index = builder.addGlobal(kWasmExnRef, true, false).exportAs('g');
  let instance = builder.instantiate();
  assertThrows(() => new WebAssembly.Global({value: "exnref", mutable: true}, null), TypeError);
  assertThrows(() => { instance.exports.g.value; }, TypeError);
})();
