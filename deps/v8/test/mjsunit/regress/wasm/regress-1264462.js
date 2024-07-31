// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This was for a bug in the instruction selector, but the minimal repro
// required the (now removed) mid-tier register allocator.
// We still keep this regression test for the future, even though the flag
// `--turbo-force-mid-tier-regalloc` doesn't exist anymore.

// Flags: --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addFunction('main', kSig_i_v).addBody([
  kExprI32Const, 0,                           // i32.const
  kSimdPrefix, kExprI8x16Splat,               // i8x16.splat
  kExprI32Const, 0,                           // i32.const
  kSimdPrefix, kExprI8x16Splat,               // i8x16.splat
  kSimdPrefix, kExprI64x2Mul, 0x01,           // i64x2.mul
  kSimdPrefix, kExprI8x16ExtractLaneS, 0x00,  // i8x16.extract_lane_s
]);
builder.toModule();
