// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

var builder = new WasmModuleBuilder();

builder.addGlobal(kWasmI32, false, false).exportAs('g_n');

builder.addMemory(32, 128);
builder.exportMemoryAs('mem', 0);

builder.addActiveDataSegment(0, [kExprI32Const, 0], [0x2a, 0x2b, 0x2c, 0x2d]);

var func_a_idx =
    builder.addFunction('wasm_A', kSig_v_i).addBody([kExprNop, kExprNop]).index;

// wasm_B calls wasm_A <param0> times.
builder.addFunction('wasm_B', kSig_v_i)
    .addBody([
      kExprLoop,
      kWasmVoid,  // while
      kExprLocalGet,
      0,  // -
      kExprIf,
      kWasmVoid,  // if <param0> != 0
      kExprLocalGet,
      0,  // -
      kExprI32Const,
      1,            // -
      kExprI32Sub,  // -
      kExprLocalSet,
      0,                      // decrease <param0>
      ...wasmI32Const(1024),  // some longer i32 const (2 byte imm)
      kExprCallFunction,
      func_a_idx,  // -
      kExprBr,
      1,         // continue
      kExprEnd,  // -
      kExprEnd,  // break
    ])
    .exportAs('main');

const instance = builder.instantiate();
const wasm_main = instance.exports.main;

function f() {
  wasm_main(42);
}
f();
