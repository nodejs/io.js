// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

function MakeBody(lane_width) {
  let select_instr = lane_width == 8 ? kExprI8x16RelaxedLaneSelect :
                     lane_width == 16 ? kExprI16x8RelaxedLaneSelect :
                     lane_width == 32 ? kExprI32x4RelaxedLaneSelect :
                     lane_width == 64 ? kExprI64x2RelaxedLaneSelect : null;
  assertNotNull(select_instr);
  return [
    // Value A: all 0-bits.
    kExprI32Const, 0x00,
    kSimdPrefix, kExprI8x16Splat,
    // Value B: all 1-bits.
    ...wasmI32Const(0xFF),
    kSimdPrefix, kExprI8x16Splat,
    // Mask: a wild mix of bit patterns.
    ...wasmS128Const([0x80, 0x7F, 0xC0, 0x3F, 0x40, 0x9F, 0x20, 0x1F,
                      0x70, 0xA0, 0xFF, 0x00, 0xFF, 0xFF, 0x10, 0x01]),
    // The main point of the test: use the mask to select lanes.
    kSimdPrefix, ...select_instr,
    // Let's see which lanes were selected for each byte.
    kSimdPrefix, kExprI8x16BitMask, 0x01,
  ];
}

builder.addFunction('test8', kSig_i_v).exportFunc().addBody(MakeBody(8));
builder.addFunction('test16', kSig_i_v).exportFunc().addBody(MakeBody(16));
builder.addFunction('test32', kSig_i_v).exportFunc().addBody(MakeBody(32));
builder.addFunction('test64', kSig_i_v).exportFunc().addBody(MakeBody(64));
const instance = builder.instantiate();

let test8 = instance.exports.test8;
let test16 = instance.exports.test16;
let test32 = instance.exports.test32;
let test64 = instance.exports.test64;

let test8_result = test8();
let test16_result = test16();
let test32_result = test32();
let test64_result = test64();

%WasmTierUpFunction(test8);
%WasmTierUpFunction(test16);
%WasmTierUpFunction(test32);
%WasmTierUpFunction(test64);

assertEquals(test8_result, test8());
assertEquals(test16_result, test16());
assertEquals(test32_result, test32());
assertEquals(test64_result, test64());
