// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(1, 1);
builder.addFunction('main', kSig_d_v)
    .addBody([
      ...wasmI32Const(-3),                            // i32.const
      kExprI32SExtendI8,                              // i32.extend8_s
      kSimdPrefix, kExprS128Load32Splat, 0x01, 0x02,  // s128.load32_splat
      kExprUnreachable,                               // unreachable
    ])
    .exportFunc();
const instance = builder.instantiate();
assertTraps(kTrapMemOutOfBounds, instance.exports.main);
