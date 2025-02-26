// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
const main_sig = builder.addType(kSig_i_iii);
const main = builder.addFunction('main', main_sig).exportAs('main');
builder.addMemory64(0, 8);

// func $main: [kWasmI32, kWasmI32, kWasmI32] -> [kWasmI32]
main.addBody([
    kExprLoop, kWasmVoid,
      kExprLocalGet, 0,
      kExprIf, kWasmVoid,
        kExprLocalGet, 0,
        kExprBr, 2,
      kExprEnd,
    kExprEnd,
    ...wasmI32Const(0),
    ...wasmI32Const(1),
    ...wasmI32Const(2),
    kExprRefFunc, main.index,
    kExprCallRef, main_sig,
    kExprF32SConvertI32,
    kNumericPrefix, kExprI64SConvertSatF32,
    kExprI64Const, 44,
    kExprI64Shl,
    kExprI32LoadMem8U, 0, ...wasmSignedLeb64(5n),
  ]);

const instance = builder.instantiate();
instance.exports.main(1, 2, 3);
