// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
d8.file.execute('test/mjsunit/value-helper.js');

(function I64x2() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction("scalar", kSig_l_llll).addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprI64Sub,
    kExprLocalGet, 3,
    kExprLocalGet, 2,
    kExprI64Sub,
    kExprI64Sub,
  ]).exportFunc();

  builder.addFunction("simd", kSig_l_llll).addLocals(kWasmS128, 5).addBody([
    kExprLocalGet, 0,
    kSimdPrefix, kExprI64x2Splat,
    kExprLocalTee, 4,
    kExprLocalGet, 3,
    kSimdPrefix, kExprI64x2Splat,
    kExprLocalTee, 5,
    kSimdPrefix, kExprI8x16Shuffle,   // 0, 3
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    kExprLocalGet, 1,
    kSimdPrefix, kExprI64x2Splat,
    kExprLocalTee, 6,
    kExprLocalGet, 2,
    kSimdPrefix, kExprI64x2Splat,
    kExprLocalTee, 7,
    kSimdPrefix, kExprI8x16Shuffle,   // 1, 2
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    ...SimdInstr(kExprI64x2Sub),
    kExprLocalTee, 8,
    kSimdPrefix, kExprI64x2ExtractLane, 0,
    kExprLocalGet, 8,
    kSimdPrefix, kExprI64x2ExtractLane, 1,
    kExprI64Sub,
  ]).exportFunc();

  const wasm = builder.instantiate().exports;
  for (let idxa = 0; idxa < uint64_array.length; ++idxa) {
    for (let idxb = 0; idxb < uint64_array.length; ++idxb) {
      const a = uint64_array[idxa];
      const b = uint64_array[idxb];
      const expected = BigInt.asUintN(64, wasm.scalar(b, a, a, b));
      const result = BigInt.asUintN(64, wasm.simd(b, a, a, b));
      assertEquals(expected, result);
    }
  }
})();

(function I64x2EvenOdd() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction("scalar", kSig_l_ll).addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprI64Mul,        // local0 * local1
    kExprI64Const, 1,
    kExprLocalGet, 0,
    kExprI64Add,        // local0 + 1
    kExprI64Const, 1,
    kExprLocalGet, 1,
    kExprI64Xor,        // local1 ^ 1
    kExprI64Mul,        // (local0 + 1) * (local1 ^ 1)
    kExprI64Sub,        // local0 + local1 - ((local0 + 1) * (local1 ^ 1))
  ]).exportFunc();

  builder.addFunction("simd", kSig_l_ll).addLocals(kWasmS128, 3).addBody([
    kExprLocalGet, 0,
    kSimdPrefix, kExprI64x2Splat,                     // [local0, local0 ]
    kExprLocalGet, 1,
    kSimdPrefix, kExprI64x2ReplaceLane, 1,            // [local0, local1 ]
    kExprLocalTee, 2,
    kExprI64Const, 1,
    kExprLocalGet, 0,
    kExprI64Add,
    kSimdPrefix, kExprI64x2Splat,                     // [local0 + 1, local0 + 1]
    kExprI64Const, 1,
    kExprLocalGet, 1,
    kExprI64Xor,
    kSimdPrefix, kExprI64x2ReplaceLane, 1,            // [local0 + 1, local1 ^ 1]
    kExprLocalTee, 3,
    kSimdPrefix, kExprI8x16Shuffle,   // even         // [local0, local0 + 1]
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kSimdPrefix, kExprI8x16Shuffle,   // odd          // [local1, local1 ^ 1]
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    ...SimdInstr(kExprI64x2Mul),                      // [local0 * local1,
    kExprLocalTee, 4,                                 //  (local0 + 1) * (local1 ^ 1)]
    kExprLocalGet, 4,
    kExprLocalGet, 4,
    kSimdPrefix, kExprI8x16Shuffle,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,   // [(local0 + 1) * (local1 ^ 1)]
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ...SimdInstr(kExprI64x2Sub),                      // [(local0 * local1) -
    kSimdPrefix, kExprI64x2ExtractLane, 0,            //  ((local0 + 1) * local1 ^ 1))]
  ]).exportFunc();

  const wasm = builder.instantiate().exports;
  for (let idxa = 0; idxa < uint64_array.length; ++idxa) {
    for (let idxb = 0; idxb < uint64_array.length; ++idxb) {
      const a = uint64_array[idxa];
      const b = uint64_array[idxb];
      const expected = BigInt.asUintN(64, wasm.scalar(a, b));
      const result = BigInt.asUintN(64, wasm.simd(a, b));
      assertEquals(expected, result);
    }
  }
})();
