// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestArrayNewS128Unreachable() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmS128, true);
  let empty_sig = builder.addType(makeSig([], []));
  builder.addFunction(undefined, empty_sig)
    .addBody([
      // The empty try will never jump to the catch making the catch block
      // unreachable already during graph construction time.
      kExprTry, kWasmVoid,
      kExprCatchAll,
        kExprI32Const, 0,
        kSimdPrefix, kExprI8x16Splat,
        kExprI32Const, 42,
        kGCPrefix, kExprArrayNew, array,  // array.new
        kExprDrop,
      kExprEnd,
  ]);
  builder.addExport('main', 0);
  const instance = builder.instantiate();
  instance.exports.main();
})();

(function TestArrayNewUnreachableNoDefault() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  let empty_sig = builder.addType(makeSig([], []));
  let array = builder.addArray(wasmRefType(empty_sig), true);
  let empty_func = builder.addFunction(undefined, empty_sig)
    .addBody([]).exportFunc();
  builder.addFunction("main", empty_sig)
    .addBody([
      // The empty try will never jump to the catch making the catch block
      // unreachable already during graph construction time.
      kExprTry, kWasmVoid,
      kExprCatchAll,
        kExprRefFunc, empty_func.index,
        kExprI32Const, 42,
        kGCPrefix, kExprArrayNew, array,  // array.new
        kExprDrop,
      kExprEnd,
  ]).exportFunc();
  const instance = builder.instantiate();
  instance.exports.main();
})();
