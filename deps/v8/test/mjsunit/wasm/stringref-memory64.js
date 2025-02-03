// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-stringref --experimental-wasm-memory64

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();
builder.addMemory(1, 1);  // Just to block memory index 0.
let $mem1 = builder.addMemory64(1, 1);

builder.addActiveDataSegment(
    1, [kExprI64Const, 0], [97, 98, 99, 0, 100, 0, 101]);

let kSig_w_li = makeSig([kWasmI64, kWasmI32], [kWasmStringRef]);
let kSig_i_wl = makeSig([kWasmStringRef, kWasmI64], [kWasmI32]);

builder.addFunction("new_wtf8", kSig_w_li).exportFunc().addBody([
  kExprLocalGet, 0,
  kExprLocalGet, 1,
  ...GCInstr(kExprStringNewUtf8), $mem1,
]);

builder.addFunction("new_wtf16", kSig_w_li).exportFunc().addBody([
  kExprLocalGet, 0,
  kExprLocalGet, 1,
  ...GCInstr(kExprStringNewWtf16), $mem1,
]);

builder.addFunction("encode_wtf8", kSig_i_wl).exportFunc().addBody([
  kExprLocalGet, 0,
  kExprLocalGet, 1,
  ...GCInstr(kExprStringEncodeWtf8), $mem1,
]);

builder.addFunction("encode_wtf8_view", kSig_i_wl).exportFunc().addBody([
  kExprLocalGet, 0,
  ...GCInstr(kExprStringAsWtf8),
  kExprLocalGet, 1,
  kExprI32Const, 0,  // start offset
  kExprI32Const, 2,  // number of bytes
  ...GCInstr(kExprStringViewWtf8EncodeWtf8), $mem1,
  kExprReturn,
]);

builder.addFunction("encode_wtf16", kSig_i_wl).exportFunc().addBody([
  kExprLocalGet, 0,
  kExprLocalGet, 1,
  ...GCInstr(kExprStringEncodeWtf16), $mem1,
]);

builder.addFunction("encode_wtf16_view", kSig_i_wl).exportFunc().addBody([
  kExprLocalGet, 0,
  ...GCInstr(kExprStringAsWtf16),
  kExprLocalGet, 1,
  kExprI32Const, 0,  // start offset
  kExprI32Const, 2,  // number of code units
  ...GCInstr(kExprStringViewWtf16Encode), $mem1,
]);

let instance = builder.instantiate();

assertEquals("ab", instance.exports.new_wtf8(0n, 2));

assertEquals("cd", instance.exports.new_wtf16(2n, 2));

assertEquals(2, instance.exports.encode_wtf8("ef", 100n));
assertEquals("ef", instance.exports.new_wtf8(100n, 2));

assertEquals(2, instance.exports.encode_wtf8_view("gh", 100n));
assertEquals("gh", instance.exports.new_wtf8(100n, 2));

assertEquals(2, instance.exports.encode_wtf16("ij", 102n));
assertEquals("ij", instance.exports.new_wtf16(102n, 2));

assertEquals(2, instance.exports.encode_wtf16_view("kl", 102n));
assertEquals("kl", instance.exports.new_wtf16(102n, 2));
