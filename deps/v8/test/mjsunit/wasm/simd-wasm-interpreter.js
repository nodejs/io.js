// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// Tests that R2R/S2R mode is not supported for Simd types in the Wasm
// interpreter.
(function testSimdSelect() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  builder.addType(makeSig([], []));
  builder.addFunction('main', 0 /* sig */)
    .addLocals(kWasmS128, 2)
    .exportFunc()
    .addBody([
      ...wasmS128Const(new Array(16).fill(0)),  // s128.const
      ...wasmS128Const(new Array(16).fill(0)),  // s128.const
      kExprI32Const, 0,
      kExprSelect,                              // select
      kExprDrop,
    ]);
  var instance = builder.instantiate();
  instance.exports.main();
})();

// Tests R2S mode Select instruction for Simd types
(function testSimdSelectR2S() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  let globalint = builder.addImportedGlobal('o', 'g', kWasmI32, true);

  builder.addType(makeSig([], []));
  builder.addFunction('main', 0 /* sig */)
    .addLocals(kWasmS128, 2)
    .exportFunc()
    .addBody([
      ...wasmS128Const(new Array(16).fill(1)),  // s128.const
      ...wasmS128Const(new Array(16).fill(0)),  // s128.const
      kExprGlobalGet, globalint,
      kExprSelect,                              // select

      // The first s128 const should be selected.
      ...wasmS128Const(new Array(16).fill(1)),
      kSimdPrefix, kExprI8x16Ne,
      kSimdPrefix, kExprI8x16AllTrue,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,
    ]);
  let gint = new WebAssembly.Global({mutable: true, value: 'i32'});
  gint.value = 1;
  var instance = builder.instantiate({o: {g: gint}});
  instance.exports.main();
})();
