// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --enable-testing-opcode-in-wasm --nowasm-tier-up
// Flags: --wasm-tier-mask-for-testing=2

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function InstanceMaker(offset) {
  var builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);

  var sig_index = builder.addType(makeSig(
      [kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
       kWasmI32],
      [kWasmI32]));
  var sig_three = builder.addType(makeSig(
      [kWasmI64, kWasmI64, kWasmI64, kWasmI64, kWasmI64, kWasmI64, kWasmI64,
       kWasmI64],
      []));

  var zero = builder.addFunction("zero", kSig_i_i);
  var one = builder.addFunction("one", sig_index);
  var two = builder.addFunction("two", kSig_v_i);
  var three = builder.addFunction("three", sig_three).addBody([]);

  zero.addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, offset]);

  one.addBody([
    kExprLocalGet, 7,
    kExprCallFunction, zero.index]);

  two.addBody([
      kExprI64Const, 0x81, 0x80, 0x80, 0x80, 0x10,
      kExprI64Const, 0x81, 0x80, 0x80, 0x80, 0x10,
      kExprI64Const, 0x81, 0x80, 0x80, 0x80, 0x10,
      kExprI64Const, 0x81, 0x80, 0x80, 0x80, 0x10,
      kExprI64Const, 0x81, 0x80, 0x80, 0x80, 0x10,
      kExprI64Const, 0x81, 0x80, 0x80, 0x80, 0x10,
      kExprI64Const, 0x81, 0x80, 0x80, 0x80, 0x10,
      kExprI64Const, 0x81, 0x80, 0x80, 0x80, 0x10,
      kExprCallFunction, three.index,
      kExprI32Const, 0,
      kExprI32Const, 0,
      kExprI32Const, 0,
      kExprI32Const, 0,
      kExprI32Const, 0,
      kExprI32Const, 0,
      kExprI32Const, 0,
      kExprI32Const, 0,
      kExprCallFunction, one.index,
      kExprDrop,
    ]).exportFunc();

  return builder.instantiate({});
}

var instance = InstanceMaker(0);
instance.exports.two();

// Regression test for crbug.com/1224882.
var instance_with_offset = InstanceMaker(4);
instance_with_offset.exports.two();
