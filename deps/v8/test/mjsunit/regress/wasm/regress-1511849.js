// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turboshaft-wasm --experimental-wasm-inlining --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function Simple() {
  const builder = new WasmModuleBuilder();

  let void_function = builder.addFunction("void", kSig_v_v)
    .addBody([]);

  let main_function = builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprTry, kWasmI32,
        kExprCallFunction, void_function.index,
        kExprI32Const, 42,
      kExprCatchAll,
        kExprCallFunction, void_function.index,
        kExprI32Const, 0,
      kExprEnd])
    .exportFunc();

  let main = builder.instantiate().exports.main;

  main();

  %WasmTierUpFunction(main);

  main();
})();
