// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-imported-strings --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(1, 1, false);
let tag0 = builder.addTag(kSig_v_v);

let kSig_i_e = builder.addType(
    makeSig([kWasmExternRef], [kWasmI32]));

let kStringTest = builder.addImport(
    'wasm:js-string', 'test', kSig_i_e);

let nop = builder.addFunction('nop', kSig_v_v).addBody([]);

builder.addFunction("main", makeSig([], [kWasmI32])).exportFunc()
  .addLocals(kWasmI32, 1)
  .addBody([
    kExprTry, kWasmI32,
      kExprLoop, kWasmVoid,
        kExprRefNull, kExternRefCode,
        kExprCallFunction, kStringTest,
        kExprBrIf, 0,
      kExprEnd,  // end of inner loop.
      kExprI32Const, 0,  // address
    kExprCatchAll,
      kExprI32Const, 42,
    kExprEnd,  // end of try-catch.
]);

let kBuiltins = { builtins: ['js-string'] };
const instance = builder.instantiate({}, kBuiltins);
console.log(instance.exports.main());
