// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestNominalTypesBasic() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  let struct1 = builder.addStruct([makeField(kWasmI32, true)]);
  let struct2 = builder.addStruct(
      [makeField(kWasmI32, true), makeField(kWasmI32, true)], struct1);

  let array1 = builder.addArray(kWasmI32, true);
  let array2 = builder.addArray(kWasmI32, true, array1);

  builder.addFunction("main", kSig_v_v)
      .addLocals(wasmRefNullType(struct1), 1)
      .addLocals(wasmRefNullType(array1), 1)
      .addBody([
        // Check that we can create a struct with implicit RTT.
        kGCPrefix, kExprStructNewDefault, struct2,
        // ...and upcast it.
        kExprLocalSet, 0,
        // Check that we can create an array with implicit RTT.
        kExprI32Const, 10,  // length
        kGCPrefix, kExprArrayNewDefault, array2,
        // ...and upcast it.
        kExprLocalSet, 1])
      .exportFunc();

  // This test is only interested in type checking.
  builder.instantiate();
})();

(function TestSubtypingDepthTooLarge() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addStruct([]);
  for (let i = 0; i < 64; i++) builder.addStruct([], i);
  assertThrows(
      () => builder.instantiate(), WebAssembly.CompileError,
      /subtyping depth is greater than allowed/);
})();
