// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Test for S128 global with initialization.
// This checks for a bug in copying the immediate values from the
// initialization expression into the globals area of the module.
(function TestS128GlobalInitialization() {
  var builder = new WasmModuleBuilder();
  var g = builder.addGlobal(
    kWasmS128, false, false,
    [kSimdPrefix, kExprS128Const,
     1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0]);

  // Check that all lanes have the right values by creating 4 functions that
  // extract each lane.
  function addGetFunction(i) {
    builder.addFunction(`get${i}`, kSig_i_v)
      .addBody([
        kExprGlobalGet, g.index,
        kSimdPrefix, kExprI32x4ExtractLane, i])
      .exportAs(`get${i}`);
  }

  for (let i = 0; i < 4; i++) {
    addGetFunction(i);
  }

  var instance = builder.instantiate();

  for (let i = 0; i < 4; i++) {
    // get0 will get lane0, which has value 1
    assertEquals(i+1, instance.exports[`get${i}`]());
  }
})();

(function TestS128GlobalImport() {
  // We want to test that a module with an imported V128 global does not crash.
  // But that is a bit tricky because:
  // 1. WebAssembly.Global({value: 'v128'}) is an error
  // 2. WebAssembly.Global of any other type is a type mismatch error
  // So here, we do 2. in order to get further along the code path, where
  // previously it would have crashed, it now checks for v128 and exits early.
  var builder = new WasmModuleBuilder();
  var g = builder.addImportedGlobal('m', 'foo', kWasmS128);
  assertThrows(() => builder.instantiate({m: {foo: 0}}));
})();
