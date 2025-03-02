// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-max-table-size=100

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let $table0 = builder.addTable(kWasmFuncRef, 1, undefined);

builder.addFunction("grow", kSig_i_i).exportFunc().addBody([
    kExprRefNull, kNullFuncRefCode,
    kExprLocalGet, 0,
    kNumericPrefix, kExprTableGrow, $table0.index,
  ]);

var instance = builder.instantiate();
assertEquals(1, instance.exports.grow(40));
assertEquals(41, instance.exports.grow(2));
assertEquals(43, instance.exports.grow(40));
assertEquals(83, instance.exports.grow(2));
assertEquals(-1, instance.exports.grow(40));
assertEquals(85, instance.exports.grow(2));
