// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder.addFunction("main", kSig_v_v)
  .addLocals(kWasmS128, 2)
  .addBody([
      kExprLocalGet, 0,
      kExprLocalSet, 0,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kSimdPrefix, kExprI8x16Swizzle,
      kExprDrop,
  ]).exportFunc();
let instance = builder.instantiate();
instance.exports.main();
