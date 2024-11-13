// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig0 = builder.addType(kSig_v_v);
let $sig7 = builder.addType(
    makeSig([], [ kWasmExternRef, kWasmS128, kWasmExternRef ]));
let $func0 = builder.addImport('imports', 'func0', $sig0);
builder.addFunction("main", $sig0).exportFunc()
  .addLocals(kWasmExternRef, 3)
  .addBody([
    kExprTry, $sig7,
      kExprCallFunction, $func0,
      kExprUnreachable,
    kExprCatchAll,
      kExprRefNull, kExternRefCode,
      ...wasmS128Const([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
      kExprRefNull, kExternRefCode,
    kExprEnd,
    kExprDrop,
    kExprDrop,
    kExprDrop,
  ]);

var instance = builder.instantiate({'imports': { 'func0': () => {} }});

assertThrows(instance.exports.main, WebAssembly.RuntimeError, /unreachable/);
