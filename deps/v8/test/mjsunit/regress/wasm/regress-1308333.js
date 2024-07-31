// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addMemory(16, 32);
// Generate function 1 (out of 1).
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprI32Const, 0xe2, 0x80, 0xae, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32And,  // i32.and
kExprCallFunction, 0x00,  // call function #0: i_iii
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32And,  // i32.and
kExprCallFunction, 0x00,  // call function #0: i_iii
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Xor,  // i32.xor
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32And,  // i32.and
kExprCallFunction, 0x00,  // call function #0: i_iii
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32And,  // i32.and
kExprCallFunction, 0x00,  // call function #0: i_iii
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32And,  // i32.and
kExprCallFunction, 0x00,  // call function #0: i_iii
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32And,  // i32.and
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32And,  // i32.and
kExprCallFunction, 0x00,  // call function #0: i_iii
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32And,  // i32.and
kExprCallFunction, 0x00,  // call function #0: i_iii
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x78,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x10,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32And,  // i32.and
kExprCallFunction, 0x00,  // call function #0: i_iii
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x7c,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprI32Const, 0x73,  // i32.const
kExprUnreachable,  // unreachable
kExprEnd  // end @353
]).exportAs("main");
let instance = builder.instantiate();
assertThrows(() => instance.exports.main(1, 2, 3));
