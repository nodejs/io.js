// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32);
builder.addFunction('main', kSig_i_iii)
    .addBody([
      kExprLocalGet, 1,                              // local.get
      kExprLocalGet, 1,                              // local.get
      kExprLocalGet, 0,                              // local.get
      kExprLocalSet, 1,                              // local.set
      kAtomicPrefix, kExprI32AtomicSub, 0x02, 0x26,  // i32.atomic.sub32
    ])
    .exportFunc();
const instance = builder.instantiate();
assertEquals(0, instance.exports.main(1, 2, 3));
