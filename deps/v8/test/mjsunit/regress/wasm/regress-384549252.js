// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-enable-sse4-2

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig0 = builder.addType(makeSig([], [kWasmF64, kWasmF64]));
let $array2 = builder.addArray(kWasmI32, false, kNoSuperType, true);
let $f = builder.addFunction('f', $sig0)
  .addLocals(kWasmS128, 1)  // $var0
  .addBody([
    kExprI32Const, 57,
    kGCPrefix, kExprArrayNewDefault, $array2,
    kGCPrefix, kExprArrayLen,
    kExprLoop, kWasmVoid,
      kExprLocalGet, 0,  // $var0
      ...SimdInstr(kExprF64x2Sqrt),
      kSimdPrefix, kExprI32x4ExtractLane, 2,
      kExprBrIf, 0,
      kExprUnreachable,
    kExprEnd,
    kExprUnreachable,
  ]).exportFunc();

const instance = builder.instantiate({});

assertTraps(kTrapUnreachable, instance.exports.f);
