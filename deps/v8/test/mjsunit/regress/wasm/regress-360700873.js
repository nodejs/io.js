// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --liftoff-only

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig2 =
    builder.addType(makeSig([], [kWasmF32, kWasmF32, kWasmF64, kWasmFuncRef]));
let $sig3 = builder.addType(
    makeSig([kWasmExternRef, kWasmF32, kWasmF32, kWasmF64, kWasmFuncRef], []));
let a0 = builder.addImport('m', 'a', kSig_v_v);
let $global0 = builder.addImportedGlobal('m', 'global0', kWasmF64, true);
builder.addDeclarativeElementSegment([a0]);
builder.addFunction('main', kSig_v_d).exportFunc().addBody([
    kExprRefNull, kExternRefCode,
    kExprTry, $sig2,
      ...wasmF32Const(0),
      ...wasmF32Const(0),
      ...wasmF64Const(0),
      kExprRefNull, kFuncRefCode,
    kExprEnd,
    kExprLoop, $sig3,
      kExprCallFunction, a0,
      kExprDrop,
      kExprDrop,
      kExprDrop,
      kExprDrop,
      kExprI64Const, 0,
      kExprRefNull, kExternRefCode,
      ...wasmF32Const(30),
      ...wasmF32Const(50),
      kExprGlobalGet, $global0,
      kExprRefFunc, a0,
      kExprBr, 0,
    kExprEnd,
  ]);

let remaining_iterations = 3;
function a() {
  if (--remaining_iterations == 0) throw new Error('finished');
  gc();
}

let global0 = new WebAssembly.Global({value: 'f64', mutable: true});

let instance = builder.instantiate({m: {a, global0}});

global0.value = 1.5e-323;  // bit pattern "0x3", cleared weak pointer
assertThrows(instance.exports.main, Error, 'finished');
