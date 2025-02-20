// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-wasm-lazy-compilation
// Flags: --enable-testing-opcode-in-wasm

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
d8.file.execute('test/mjsunit/value-helper.js');

(function I8x16UpperToLowerReduce() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const shared_simd = [
    kExprLocalGet, 0,
    kSimdPrefix, kExprI8x16Splat,
    kExprLocalGet, 1,
    kSimdPrefix, kExprI8x16ReplaceLane, 1,
    kExprLocalGet, 2,
    kSimdPrefix, kExprI8x16ReplaceLane, 2,
    kExprLocalGet, 3,
    kSimdPrefix, kExprI8x16ReplaceLane, 3,
    kExprLocalGet, 1,
    kSimdPrefix, kExprI8x16ReplaceLane, 5,
    kExprLocalGet, 2,
    kSimdPrefix, kExprI8x16ReplaceLane, 6,
    kExprLocalGet, 3,
    kSimdPrefix, kExprI8x16ReplaceLane, 7,
    kExprLocalGet, 1,
    kSimdPrefix, kExprI8x16ReplaceLane, 9,
    kExprLocalGet, 2,
    kSimdPrefix, kExprI8x16ReplaceLane, 10,
    kExprLocalGet, 3,
    kSimdPrefix, kExprI8x16ReplaceLane, 11,
    kExprLocalGet, 1,
    kSimdPrefix, kExprI8x16ReplaceLane, 13,
    kExprLocalGet, 2,
    kSimdPrefix, kExprI8x16ReplaceLane, 14,
    kExprLocalGet, 3,
    kSimdPrefix, kExprI8x16ReplaceLane, 15,
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // upper-to-lower 8x16
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    kSimdPrefix, kExprI8x16Add,                       // first step of reduction
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // upper-to-lower 8x8
    0x04, 0x05, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    kSimdPrefix, kExprI8x16Add,                       // second step of reduction
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // upper-to-lower 8x4
    0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    kSimdPrefix, kExprI8x16Add,                       // third step of reduction
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // upper-to-lower 16x2
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    kSimdPrefix, kExprI8x16Add,                       // final step of reduction
  ]
  const shared_scalar = wasmI32Const(0xFF).concat([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32And,
  ]);

  const s8x16 = shared_simd.concat([kSimdPrefix, kExprI8x16ExtractLaneS, 0]);
  const u8x16 = shared_simd.concat([kSimdPrefix, kExprI8x16ExtractLaneU, 0]);
  builder.addFunction("sadd_reduce", kSig_i_iiii).addLocals(kWasmS128, 1).addBody(s8x16).exportFunc();
  builder.addFunction("uadd_reduce", kSig_i_iiii).addLocals(kWasmS128, 1).addBody(u8x16).exportFunc();

  const signed_scalar = shared_scalar.concat([kExprI32SExtendI8]);
  const unsigned_scalar = shared_scalar.concat([]);
  builder.addFunction("sadd", kSig_i_iiii).addBody(signed_scalar).exportFunc();
  builder.addFunction("uadd", kSig_i_iiii).addBody(unsigned_scalar).exportFunc();

  const wasm = builder.instantiate().exports;
  for (let a of int8_array) {
    for (let b of int8_array) {
      for (let c of int8_array) {
        for (let d of int8_array) {
          assertEquals(wasm.sadd(a, b, c, d),
                       wasm.sadd_reduce(a, b, c, d));
          assertEquals(wasm.uadd(a, b, c, d),
                       wasm.uadd_reduce(a, b, c, d));
        }
      }
    }
  }
})();

(function I16x8UpperToLowerReduce() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const shared_simd = [
    kExprLocalGet, 0,
    kSimdPrefix, kExprI16x8Splat,
    kExprLocalGet, 1,
    kSimdPrefix, kExprI16x8ReplaceLane, 1,
    kExprLocalGet, 2,
    kSimdPrefix, kExprI16x8ReplaceLane, 2,
    kExprLocalGet, 3,
    kSimdPrefix, kExprI16x8ReplaceLane, 3,
    kExprLocalGet, 1,
    kSimdPrefix, kExprI16x8ReplaceLane, 5,
    kExprLocalGet, 2,
    kSimdPrefix, kExprI16x8ReplaceLane, 6,
    kExprLocalGet, 3,
    kSimdPrefix, kExprI16x8ReplaceLane, 7,
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // upper-to-lower 16x8
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprI16x8Add),                       // first step of reduction
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // upper-to-lower 16x4
    0x04, 0x05, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprI16x8Add),                       // second step of reduction
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // upper-to-lower 8x2
    0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprI16x8Add),                       // final step of reduction
  ];

  const shared_scalar = wasmI32Const(0xFFFF).concat([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32And,
  ]);

  const s16x8 = shared_simd.concat([kSimdPrefix, kExprI16x8ExtractLaneS, 0]);
  const u16x8 = shared_simd.concat([kSimdPrefix, kExprI16x8ExtractLaneU, 0]);
  builder.addFunction("sadd_reduce", kSig_i_iiii).addLocals(kWasmS128, 1).addBody(s16x8).exportFunc();
  builder.addFunction("uadd_reduce", kSig_i_iiii).addLocals(kWasmS128, 1).addBody(u16x8).exportFunc();

  const signed_scalar = shared_scalar.concat([kExprI32SExtendI16]);
  const unsigned_scalar = shared_scalar.concat([]);
  builder.addFunction("sadd", kSig_i_iiii).addBody(signed_scalar).exportFunc();
  builder.addFunction("uadd", kSig_i_iiii).addBody(unsigned_scalar).exportFunc();

  const wasm = builder.instantiate().exports;
  for (let a of int16_array) {
    for (let b of int16_array) {
      for (let c of int16_array) {
        for (let d of int16_array) {
          assertEquals(wasm.sadd(a, b, c, d),
                       wasm.sadd_reduce(a, b, c, d));
          assertEquals(wasm.uadd(a, b, c, d),
                       wasm.uadd_reduce(a, b, c, d));
        }
      }
    }
  }
})();

(function I32x4UpperToLowerReduce() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction("add_reduce", kSig_i_iiii).addLocals(kWasmS128, 1).addBody([
    kExprLocalGet, 0,
    kSimdPrefix, kExprI32x4Splat,
    kExprLocalGet, 1,
    kSimdPrefix, kExprI32x4ReplaceLane, 1,
    kExprLocalGet, 2,
    kSimdPrefix, kExprI32x4ReplaceLane, 2,
    kExprLocalGet, 3,
    kSimdPrefix, kExprI32x4ReplaceLane, 3,
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // upper-to-lower 32x4
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprI32x4Add),                       // first step of reduction
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // upper-to-lower 32x2
    0x04, 0x05, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprI32x4Add),                       // final step of reduction
    kSimdPrefix, kExprI32x4ExtractLane, 0,
  ]).exportFunc();

  const wasm = builder.instantiate().exports;
  for (let idxa = 0; idxa < int32_array.length; ++idxa) {
    for (let idxb = 0; idxb < int32_array.length; ++idxb) {
      const idxc = (idxa + 1) % int32_array.length;
      const idxd = (idxb + 1) % int32_array.length;
      const a = int32_array[idxa];
      const b = int32_array[idxb];
      const c = int32_array[idxc];
      const d = int32_array[idxd];
      const expected = 0xFFFFFFFF & (a + b + c + d);
      assertEquals(expected, wasm.add_reduce(a, b, c, d));
    }
  }
})();

(function F32x4UpperToLowerReduce() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction("add_reduce", kSig_f_ffff).addLocals(kWasmS128, 1).addBody([
    kExprLocalGet, 0,
    kSimdPrefix, kExprF32x4Splat,
    kExprLocalGet, 1,
    kSimdPrefix, kExprF32x4ReplaceLane, 1,
    kExprLocalGet, 2,
    kSimdPrefix, kExprF32x4ReplaceLane, 2,
    kExprLocalGet, 3,
    kSimdPrefix, kExprF32x4ReplaceLane, 3,
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // upper-to-lower 32x4
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprF32x4Add),                       // first step of reduction
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // upper-to-lower 32x2
    0x04, 0x05, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprF32x4Add),                       // final step of reduction
    kSimdPrefix, kExprF32x4ExtractLane, 0,
  ]).exportFunc();

  builder.addFunction("add", kSig_f_ffff).addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 2,
    kExprF32Add,
    kExprLocalGet, 1,
    kExprLocalGet, 3,
    kExprF32Add,
    kExprF32Add,
  ]).exportFunc();

  const wasm = builder.instantiate().exports;
  for (let idxa = 0; idxa < float32_array.length; ++idxa) {
    for (let idxb = 0; idxb < float32_array.length; ++idxb) {
      const idxc = (idxa + 1) % float32_array.length;
      const idxd = (idxb + 1) % float32_array.length;
      const a = float32_array[idxa];
      const b = float32_array[idxb];
      const c = float32_array[idxc];
      const d = float32_array[idxd];
      assertEquals(wasm.add(a, b, c, d), wasm.add_reduce(a, b, c, d));
    }
  }
})();

(function F32x4PairwiseReduce() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction("add_reduce", kSig_f_ffff).addLocals(kWasmS128, 1).addBody([
    kExprLocalGet, 0,
    kSimdPrefix, kExprF32x4Splat,
    kExprLocalGet, 1,
    kSimdPrefix, kExprF32x4ReplaceLane, 1,
    kExprLocalGet, 2,
    kSimdPrefix, kExprF32x4ReplaceLane, 2,
    kExprLocalGet, 3,
    kSimdPrefix, kExprF32x4ReplaceLane, 3,
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // pairwise 32x4
    0x04, 0x05, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprF32x4Add),                       // first step of reduction
    kExprLocalTee, 4,
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,                   // pairwise 32x2
    0x08, 0x09, 0xa, 0xb, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprF32x4Add),                       // final step of reduction
    kSimdPrefix, kExprF32x4ExtractLane, 0,
  ]).exportFunc();

  builder.addFunction("add", kSig_f_ffff).addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprF32Add,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprF32Add,
    kExprF32Add,
  ]).exportFunc();

  const wasm = builder.instantiate().exports;
  for (let idxa = 0; idxa < float32_array.length; ++idxa) {
    for (let idxb = 0; idxb < float32_array.length; ++idxb) {
      const idxc = (idxa + 1) % float32_array.length;
      const idxd = (idxb + 1) % float32_array.length;
      const a = float32_array[idxa];
      const b = float32_array[idxb];
      const c = float32_array[idxc];
      const d = float32_array[idxd];
      assertEquals(wasm.add(a, b, c, d), wasm.add_reduce(a, b, c, d));
    }
  }
})();

(function I64x2Reduce() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction("add_reduce", kSig_l_ll).addLocals(kWasmS128, 1).addBody([
    kExprLocalGet, 0,
    kSimdPrefix, kExprI64x2Splat,
    kExprLocalGet, 1,
    kSimdPrefix, kExprI64x2ReplaceLane, 1,
    kExprLocalTee, 2,
    kExprLocalGet, 2,
    kExprLocalGet, 2,
    kSimdPrefix, kExprI8x16Shuffle,                   // 64x2
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprI64x2Add),                       // final step of reduction
    kSimdPrefix, kExprI64x2ExtractLane, 0,
  ]).exportFunc();

  const wasm = builder.instantiate().exports;
  for (let idxa = 0; idxa < uint64_array.length; ++idxa) {
    for (let idxb = 0; idxb < uint64_array.length; ++idxb) {
      const a = uint64_array[idxa];
      const b = uint64_array[idxb];
      const expected = BigInt.asUintN(64, a + b);
      assertEquals(expected, BigInt.asUintN(64, wasm.add_reduce(a, b)));
    }
  }
})();

(function F64x2Reduce() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction("add_reduce", kSig_d_dd).addLocals(kWasmS128, 1).addBody([
    kExprLocalGet, 0,
    kSimdPrefix, kExprF64x2Splat,
    kExprLocalGet, 1,
    kSimdPrefix, kExprF64x2ReplaceLane, 1,
    kExprLocalTee, 2,
    kExprLocalGet, 2,
    kExprLocalGet, 2,
    kSimdPrefix, kExprI8x16Shuffle,                   // 64x2
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprF64x2Add),                       // final step of reduction
    kSimdPrefix, kExprF64x2ExtractLane, 0,
  ]).exportFunc();

  builder.addFunction("add", kSig_d_dd).addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprF64Add,
  ]).exportFunc();

  const wasm = builder.instantiate().exports;
  for (let idxa = 0; idxa < float64_array.length; ++idxa) {
    for (let idxb = 0; idxb < float64_array.length; ++idxb) {
      const a = float64_array[idxa];
      const b = float64_array[idxb];
      assertEquals(wasm.add(a, b), wasm.add_reduce(a, b));
    }
  }
})();
