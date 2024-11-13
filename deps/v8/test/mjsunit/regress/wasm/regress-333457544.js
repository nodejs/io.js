// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(kSig_i_iii);
builder.addTable(kWasmFuncRef, 0, 0);
builder.addFunction('main', kSig_i_iii).addBody([
  kExprI32Const, 0xad, 0x0e,        // i32.const
  kExprI32Const, 0x96, 0x01,        // i32.const
  kExprI32Const, 0xca, 0xe6, 0x1e,  // i32.const
  kExprI32Const, 0x00,              // i32.const
  kExprCallIndirect, 0x00, 0x00,    // call_indirect sig #0: i_iii
]).exportFunc();
const instance = builder.instantiate();
assertTraps(kTrapTableOutOfBounds, instance.exports.main);
