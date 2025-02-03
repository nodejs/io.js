// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig1 = builder.addType(kSig_v_v);
let $struct = builder.nextTypeIndex();
/* $struct */ builder.addStruct([], kNoSuperType, false);
let $array = builder.addArray(kWasmI8, false, kNoSuperType, true);
let $sig = builder.addType(makeSig([], [wasmRefType($struct), wasmRefType($array)]));
let func = builder.addFunction('func', $sig)
  .exportFunc()
  .addLocals(kWasmF32, 1)
  .addBody([
    kExprBlock, $sig,
      kExprI32Const, 0,  // will be dropped on the `br`
      kGCPrefix, kExprStructNew, $struct,
      kGCPrefix, kExprArrayNewFixed, $array, 0,
      kExprBr, 0,
    kExprEnd,
  ]);

let consume_struct =
    builder.addFunction('consume_struct', makeSig([wasmRefType($struct)], []))
        .exportFunc()
        .addBody([]);

let consume_array =
    builder.addFunction('consume_array', makeSig([wasmRefType($array)], []))
        .exportFunc()
        .addBody([]);

let instance = builder.instantiate({});
// Check that `func` returns the expected objects.
let [struct, array] = instance.exports.func();
instance.exports.consume_struct(struct);
instance.exports.consume_array(array);
