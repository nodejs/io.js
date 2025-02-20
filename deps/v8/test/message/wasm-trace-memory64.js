// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --trace-wasm-memory --no-liftoff

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const GB = 1024 * 1024 * 1024;
const BIG_OFFSET = 4294970000n; // 0x100000a90n
const BIG_OFFSET_LEB = [0x90, 0x95, 0x80, 0x80, 0x10];

var builder = new WasmModuleBuilder();
builder.addMemory64(5 * GB / kPageSize);

// Functions for testing big offsets.
builder.addFunction('load', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, ...BIG_OFFSET_LEB, kExprDrop])
    .exportFunc();
builder.addFunction('load8', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprI32LoadMem8U, 0, ...BIG_OFFSET_LEB, kExprDrop])
    .exportFunc();
builder.addFunction('loadf', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprF32LoadMem, 0, ...BIG_OFFSET_LEB, kExprDrop])
    .exportFunc();
builder.addFunction('store', kSig_v_li)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0,  ...BIG_OFFSET_LEB])
    .exportFunc();
builder.addFunction('store8', kSig_v_li)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem8, 0, ...BIG_OFFSET_LEB])
    .exportFunc();
builder.addFunction('load128', kSig_v_l)
    .addBody([kExprLocalGet, 0, kSimdPrefix, kExprS128LoadMem, 0, ...BIG_OFFSET_LEB, kExprDrop])
    .exportFunc();
// SIMD is not exposed to JS, so use splat to construct a s128 value.
builder.addFunction('store128', kSig_v_li)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kSimdPrefix, kExprI32x4Splat, kSimdPrefix, kExprS128StoreMem, 0, ...BIG_OFFSET_LEB])
    .exportFunc();
// We add functions after, rather than sorting in some order, so as to keep
// the .out changes small (due to function index).
builder.addFunction('load16', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprI32LoadMem16U, 0, ...BIG_OFFSET_LEB, kExprDrop])
    .exportFunc();
builder.addFunction('load64', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprI64LoadMem, 0, ...BIG_OFFSET_LEB, kExprDrop])
    .exportFunc();
builder.addFunction('loadf64', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprF64LoadMem, 0, ...BIG_OFFSET_LEB, kExprDrop])
    .exportFunc();

// Functions for testing big indexes.
builder.addFunction('load_L', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0, kExprDrop])
    .exportFunc();
builder.addFunction('load8_L', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprI32LoadMem8U, 0, 0, kExprDrop])
    .exportFunc();
builder.addFunction('loadf_L', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprF32LoadMem, 0, 0, kExprDrop])
    .exportFunc();
builder.addFunction('store_L', kSig_v_li)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0,  0])
    .exportFunc();
builder.addFunction('store8_L', kSig_v_li)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem8, 0, 0])
    .exportFunc();
builder.addFunction('load128_L', kSig_v_l)
    .addBody([kExprLocalGet, 0, kSimdPrefix, kExprS128LoadMem, 0, 0, kExprDrop])
    .exportFunc();
// SIMD is not exposed to JS, so use splat to construct a s128 value.
builder.addFunction('store128_L', kSig_v_li)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kSimdPrefix, kExprI32x4Splat, kSimdPrefix, kExprS128StoreMem, 0, 0])
    .exportFunc();
// We add functions after, rather than sorting in some order, so as to keep
// the .out changes small (due to function index).
builder.addFunction('load16_L', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprI32LoadMem16U, 0, 0, kExprDrop])
    .exportFunc();
builder.addFunction('load64_L', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprI64LoadMem, 0, 0, kExprDrop])
    .exportFunc();
builder.addFunction('loadf64_L', kSig_v_l)
    .addBody([kExprLocalGet, 0, kExprF64LoadMem, 0, 0, kExprDrop])
    .exportFunc();

var module = builder.instantiate();

module.exports.load(4n)
module.exports.load8(1n);
module.exports.store(4n, 0x12345678);
module.exports.load(2n);
module.exports.load8(6n);
module.exports.loadf(2n);
module.exports.store8(4n, 0xab);
module.exports.load(2n);
module.exports.loadf(2n);
module.exports.store128(4n, 0xbeef);
module.exports.load128(2n);
module.exports.load16(4n);
module.exports.load64(2n);
module.exports.loadf64(2n);

module.exports.load_L(BIG_OFFSET + 4n)
module.exports.load8_L(BIG_OFFSET + 1n);
module.exports.store_L(BIG_OFFSET + 4n, 0x12345678);
module.exports.load_L(BIG_OFFSET + 2n);
module.exports.load8_L(BIG_OFFSET + 6n);
module.exports.loadf_L(BIG_OFFSET + 2n);
module.exports.store8_L(BIG_OFFSET + 4n, 0xab);
module.exports.load_L(BIG_OFFSET + 2n);
module.exports.loadf_L(BIG_OFFSET + 2n);
module.exports.store128_L(BIG_OFFSET + 4n, 0xbeef);
module.exports.load128_L(BIG_OFFSET + 2n);
module.exports.load16_L(BIG_OFFSET + 4n);
module.exports.load64_L(BIG_OFFSET + 2n);
module.exports.loadf64_L(BIG_OFFSET + 2n);
