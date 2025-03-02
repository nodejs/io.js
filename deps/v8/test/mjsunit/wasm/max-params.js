// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestMaxParameters() {
  print(arguments.callee.name);
  for (let [type, value] of [
           [kWasmI32, 0], [kWasmI64, 0n], [kWasmF32, 0], [kWasmF64, 0]]) {
    for (let num_params
             of [kSpecMaxFunctionParams, kSpecMaxFunctionParams + 1]) {
      let builder = new WasmModuleBuilder();
      let sig = builder.addType(makeSig(Array(num_params).fill(type), []));
      builder.addFunction('f', sig).exportFunc().addBody([]);
      if (num_params > kSpecMaxFunctionParams) {
        assertThrows(
            () => builder.toModule(), WebAssembly.CompileError,
            /param count of [0-9]+ exceeds internal limit/);
        continue;
      }
      let instance = builder.instantiate();
      let params = new Array(num_params).fill(value);
      instance.exports.f(...params);
    }
  }
})();
