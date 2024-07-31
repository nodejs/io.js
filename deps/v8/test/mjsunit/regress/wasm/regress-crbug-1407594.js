// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder.addMemory(1, 1);
builder.addActiveDataSegment(0, [kExprI32Const, 0], [0x78, 0x56, 0x34, 0x12]);

let spiller = builder.addFunction('spiller', kSig_v_v).addBody([]);

builder.addFunction('main', kSig_l_v)
    .exportFunc()
    .addLocals(kWasmI64, 1)
    .addBody([
      // Initialize {var0} to 0x12345678 via a zero-extended 32-bit load.
      kExprI32Const, 0,
      kExprI64LoadMem32U, 2, 0,
      kExprLocalSet, 0,
      kExprCallFunction, spiller.index,
      // The observable effect of this loop is that {var0} is left-shifted
      // until it ends in 0x..000000. The details are specifically crafted
      // to recreate a particular pattern of spill slot moves.
      kExprLoop, kWasmVoid,
        kExprI32Const, 0,
        kExprI32LoadMem, 2, 0,
        kExprI32Eqz,
        // This block is never taken; it only serves to influence register
        // allocator choices.
        kExprIf, kWasmVoid,
          kExprLocalGet, 0,
          kExprI64Const, 1,
          kExprI64And,
          kExprLocalSet, 0,
        kExprEnd,  // if
        kExprLocalGet, 0,
        kExprI64Const, 1,
        kExprI64And,
        kExprI64Eqz,
        // This block is always taken; it is conditional in order to influence
        // register allocator choices.
        kExprIf, kWasmVoid,
          kExprLocalGet, 0,
          kExprI64Const, 8,
          kExprI64Shl,
          kExprLocalSet, 0,
        kExprEnd,  // if
        kExprBlock, kWasmVoid,
          kExprLocalGet, 0,
          ...wasmI64Const(0xFFFFFF),
          kExprI64And,
          kExprI64Eqz,
          kExprI32Eqz,
          kExprCallFunction, spiller.index,
          kExprBrIf, 1,
        kExprEnd,  // block
        kExprCallFunction, spiller.index,
      kExprEnd,  // loop
      kExprLocalGet, 0,
    ]);

let instance = builder.instantiate();
assertEquals("12345678000000", instance.exports.main().toString(16));
