// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// This test case reproduces a case in Turbofan where the WasmTyper was
// flip-flopping between two different states never reaching a fix point.
const builder = new WasmModuleBuilder();
let $array1 = builder.addArray(kWasmI16, true, kNoSuperType, true);
let loopSig = builder.addType(makeSig([kWasmFuncRef], []));
let funcEndless = builder.addFunction('funcEndless', kSig_v_v).exportFunc()
  .addLocals(kWasmFuncRef, 1)  // $var0
  .addBody([
    kExprRefNull, kFuncRefCode,
    kExprLoop, loopSig,
      kExprLocalTee, 0,  // $var0
      kExprBrOnNonNull, 0,
      kExprLocalGet, 0,  // $var0
      kExprRefAsNonNull,
      kExprBrOnNonNull, 0,
    kExprEnd,

    // Use something with a gc prefix to enable the WasmTyper.
    kExprRefNull, $array1,
    kGCPrefix, kExprArrayLen,
    kExprDrop,
  ]);

const instance = builder.instantiate();
assertTraps(kTrapNullDereference, () => instance.exports.funcEndless(null));
