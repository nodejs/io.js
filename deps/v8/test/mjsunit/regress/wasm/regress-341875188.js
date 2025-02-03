// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addFunction('main', kSig_v_v)
  .addLocals(kWasmS128, 2)
.addBody([
    // Comments show the Liftoff trace on x64 that leads to the DCHECK.
    // []; [v128:s0x30, v128:s0x40]
    kExprLocalGet, 0,
    // []; [v128:s0x30, v128:s0x40, v128:xmm0]
    kExprLocalTee, 0,
    // []; [v128:xmm0, v128:s0x40, v128:xmm0]
    kExprLocalGet, 0,
    // []; [v128:xmm0, v128:s0x40, v128:xmm0, v128:xmm0]
    kExprLocalGet, 0,
    // []; [v128:xmm0, v128:s0x40, v128:xmm0, v128:xmm0, v128:xmm0]
    kExprLocalGet, 1,
    // []; [v128:xmm0, v128:s0x40, v128:xmm0, v128:xmm0, v128:xmm0, v128:xmm1]
    kExprLocalSet, 0,
    // []; [v128:xmm1, v128:s0x40, v128:xmm0, v128:xmm0, v128:xmm0]
    kSimdPrefix, kExprS128Select,
    // Now src1 == src2 == src3. Since src3 (xmm0) is free, we reuse it for dst,
    // but it also aliases src2 which is not supported.
    kExprDrop,
]).exportFunc();

const module = builder.instantiate();
module.exports.main();
