// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-exnref

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function testNonNullRefWrapperNullCheck() {
  print(arguments.callee.name);
  for (let [name, type] of Object.entries({
    'extern': kWasmExternRef,
    'any': kWasmAnyRef,
    'func': kWasmFuncRef,
    'array': kWasmArrayRef,
    'exn': kWasmExnRef,
  })) {
    print(`- ${name}`);
    let builder = new WasmModuleBuilder();
    builder.addFunction('test', makeSig([wasmRefType(type)], []))
      .addBody([kExprUnreachable])
      .exportFunc();

    let instance = builder.instantiate({});
    let wasm = instance.exports;
    assertThrows(() => wasm.test(null), TypeError,
                 /type incompatibility when transforming from\/to JS/);
  }
})();
