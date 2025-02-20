// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-wasm-lazy-compilation
// Flags: --enable-testing-opcode-in-wasm

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Make sure turboshaft bails out graciously for non-implemented features.
(function I64Identity() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("id", makeSig([kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0n, wasm.id(0n));
  assertEquals(1n, wasm.id(1n));
  assertEquals(-1n, wasm.id(-1n));
  assertEquals(0x123456789ABCn, wasm.id(0x123456789ABCn));
  assertEquals(-0x123456789ABCn, wasm.id(-0x123456789ABCn));
})();

(function I64Constants() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("main", makeSig([], [kWasmI64, kWasmI64, kWasmI64]))
    .addBody([
      ...wasmI64Const(0),
      ...wasmI64Const(-12345),
      ...wasmI64Const(0x123456789ABCDEFn),
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals([0n, -12345n, 0x123456789ABCDEFn], wasm.main());
})();

(function I64Multiplication() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("mul", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Mul,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0n, wasm.mul(0n, 5n));
  assertEquals(0n, wasm.mul(5n, 0n));
  assertEquals(5n, wasm.mul(1n, 5n));
  assertEquals(-5n, wasm.mul(5n, -1n));
  assertEquals(35n, wasm.mul(-5n, -7n));
  assertEquals(0xfffffffffn * 0xfn, wasm.mul(0xfffffffffn, 0xfn));
})();

(function I64Addition() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("add", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Add,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(3n, wasm.add(1n, 2n));
  assertEquals(0n, wasm.add(100n, -100n));
  assertEquals(0x12345678n + 0xABCDEF1234n,
               wasm.add(0x12345678n, 0xABCDEF1234n));
})();

(function I64Subtraction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("sub", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Sub,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(-1n, wasm.sub(1n, 2n));
  assertEquals(200n, wasm.sub(100n, -100n));
  assertEquals(0x12345678n - 0xABCDEF1234n,
               wasm.sub(0x12345678n, 0xABCDEF1234n));
  assertEquals(0n, wasm.sub(0x123456789ABCDEFn, 0x123456789ABCDEFn));
})();

(function I64BitAnd() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("and", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64And,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(
    0b10101010_00000000_11111111_01010101n, wasm.and(
    0b10101010_00000000_11111111_01010101n,
    0b10101010_00000000_11111111_01010101n));
  assertEquals(
    0b10101010_00000000_01010101_00000000n, wasm.and(
    0b10101010_00000000_11111111_01010101n,
    0b11111111_11111111_01010101_00000000n));
})();

(function I64BitOr() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("or", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Ior,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(
    0b10101010_00000000_11111111_01010101n, wasm.or(
    0b10101010_00000000_11111111_01010101n,
    0b10101010_00000000_11111111_01010101n));
  assertEquals(
    0b11111111_11111111_11111111_01010101n, wasm.or(
    0b10101010_00000000_11111111_01010101n,
    0b11111111_11111111_01010101_00000000n));
})();

(function I64BitXor() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("xor", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Xor,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(
    0b10101010_00000000_11111111_01010101n, wasm.xor(
    0b10101010_00000000_11111111_01010101n,
    0b00000000_00000000_00000000_00000000n));
  assertEquals(
    0b11111111_11111111_11111111_01010101n, wasm.xor(
    0b10101010_00000000_11111111_01010101n,
    0b01010101_11111111_00000000_00000000n));
})();

(function I64BitShl() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("shl", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Shl,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0x123456789n, wasm.shl(0x123456789n, 0n));
  assertEquals(0x1234567890000n, wasm.shl(0x123456789n, 16n));
  assertEquals(0x3456789000000000n, wasm.shl(0x123456789n, 36n));
  assertEquals(31n << 56n, wasm.shl(31n, -8n));
  assertEquals(31n << 1n, wasm.shl(31n, 65n));
})();

(function I64BitShrs() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("shrs", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64ShrS,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0x123456789n, wasm.shrs(0x123456789n, 0n));
  assertEquals(0x12345n, wasm.shrs(0x123456789n, 16n));
  assertEquals(-0x2n, wasm.shrs(-0x123456789n, 32n));
  assertEquals(-0x1n, wasm.shrs(-0x123456789n, 33n));
  assertEquals(-0x1n, wasm.shrs(-0x123456789n, 40n));
  assertEquals(-0x123456789n, wasm.shrs(-0x123456789n, 64n));
  assertEquals(31n >> 56n, wasm.shrs(31n, -8n));
  assertEquals(31n >> 1n, wasm.shrs(31n, 65n));
})();

(function I64BitShrU() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("shru", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64ShrU,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0x123456789n, wasm.shru(0x123456789n, 0n));
  assertEquals(0x12345n, wasm.shru(0x123456789n, 16n));
  assertEquals(0xFFFFFFFEn, wasm.shru(-0x123456789n, 32n));
  assertEquals(0xFn, wasm.shru(-0x123456789n, 60n));
  assertEquals(-0x123456789n, wasm.shru(-0x123456789n, 64n));
  assertEquals(31n >> 56n, wasm.shru(31n, -8n));
  assertEquals(31n >> 1n, wasm.shru(31n, 65n));
})();

(function I64BitRol() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("rol", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Rol,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0x123456789n, wasm.rol(0x123456789n, 0n));
  assertEquals(0x1234567890000n, wasm.rol(0x123456789n, 16n));
  assertEquals(0x3456789000000012n, wasm.rol(0x123456789n, 36n));
  assertEquals(0x34567890ABCDEF12n, wasm.rol(0xABCDEF1234567890n, 32n));
  assertEquals(0x4D5E6F78091A2B3Cn, wasm.rol(0x123456789ABCDEF0n, 31n));
  assertEquals(0x3579BDE02468ACF1n, wasm.rol(0x123456789ABCDEF0n, 33n));
  assertEquals(31n << 56n, wasm.rol(31n, -8n));
  assertEquals(31n << 1n, wasm.rol(31n, 65n));
})();

(function I64BitRolStaticRhs() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let tests = [
    // [lhs, rhs, expected]
    [0x123456789n, 0n, 0x123456789n],
    [0x12345678_11111111n, 32n, 0x11111111_12345678n],
    [0x1_23456789n, 16n, 0x12345_67890000n],
    [0x1_23456789n, 36n, 0x34567890_00000012n],
    [31n, -8, 31n << 56n],
    [31n, 65n, 31n << 1n],
  ];

  for (const [lhs, rhs, expected] of tests) {
    builder.addFunction(`rol${rhs}`, makeSig([kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      ...wasmI64Const(rhs),
      kExprI64Rol,
    ])
    .exportFunc();
  }

  let wasm = builder.instantiate().exports;

  for (const [lhs, rhs, expected] of tests) {
    print(`test i64.rol(${lhs}, ${rhs}) == ${expected}`);
    assertEquals(expected, wasm[`rol${rhs}`](lhs));
  }
})();

(function I64BitRor() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("ror", makeSig([kWasmI64, kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Ror,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0x123456789n, wasm.ror(0x123456789n, 0n));
  assertEquals(0x6789000000012345n, wasm.ror(0x123456789n, 16n));
  assertEquals(0x1234567890000000n, wasm.ror(0x123456789n, 36n));
  assertEquals(0x34567890ABCDEF12n, wasm.ror(0xABCDEF1234567890n, 32n));
  assertEquals(0x3579BDE02468ACF1n, wasm.ror(0x123456789ABCDEF0n, 31n));
  assertEquals(0x4D5E6F78091A2B3Cn, wasm.ror(0x123456789ABCDEF0n, 33n));
  assertEquals(31n << 8n, wasm.ror(31n, -8n));
  assertEquals(31n << 1n, wasm.ror(31n, 127n));
})();

(function I64Equals() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("eq", makeSig([kWasmI64, kWasmI64], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Eq,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(1, wasm.eq(0n, 0n));
  assertEquals(1, wasm.eq(-123n, -123n));
  assertEquals(0, wasm.eq(-123n, 123n));
  assertEquals(0, wasm.eq(0x12345678_87654321n, 0x87654321_12345678n));
  assertEquals(0, wasm.eq(0x12345678_87654321n, 0x1234567A_87654321n));
  assertEquals(0, wasm.eq(0x12345678_87654321n, 0x12345678_8765432An));
})();

(function I64NotEquals() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("ne", makeSig([kWasmI64, kWasmI64], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64Ne,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0, wasm.ne(0n, 0n));
  assertEquals(0, wasm.ne(-123n, -123n));
  assertEquals(1, wasm.ne(-123n, 123n));
  assertEquals(1, wasm.ne(0x12345678_87654321n, 0x87654321_12345678n));
  assertEquals(1, wasm.ne(0x12345678_87654321n, 0x1234567A_87654321n));
  assertEquals(1, wasm.ne(0x12345678_87654321n, 0x12345678_8765432An));
})();

(function I64LessThanSigned() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("lts", makeSig([kWasmI64, kWasmI64], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64LtS,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0, wasm.lts(0n, 0n));
  assertEquals(1, wasm.lts(-123n, 123n));
  assertEquals(0, wasm.lts(123n, -123n));
  assertEquals(1, wasm.lts(0x12345678_12488421n, 0x12488421_12345678n));
  assertEquals(0, wasm.lts(0x12488421_12345678n, 0x12345678_12488421n));
  assertEquals(0, wasm.lts(0x12345678_87654321n, 0x12345678_87654320n));
  assertEquals(1, wasm.lts(0x12345678_87654321n, 0x12345678_87654322n));
})();

(function I64LessThanOrEqualSigned() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("les", makeSig([kWasmI64, kWasmI64], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64LeS,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(1, wasm.les(0n, 0n));
  assertEquals(1, wasm.les(-123n, 123n));
  assertEquals(0, wasm.les(123n, -123n));
  assertEquals(1, wasm.les(0x12345678_12488421n, 0x12488421_12345678n));
  assertEquals(0, wasm.les(0x12488421_12345678n, 0x12345678_12488421n));
  assertEquals(0, wasm.les(0x12345678_87654321n, 0x12345678_87654320n));
  assertEquals(1, wasm.les(0x12345678_87654321n, 0x12345678_87654322n));
})();

(function I64LessThanUnsigned() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("ltu", makeSig([kWasmI64, kWasmI64], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64LtU,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0, wasm.ltu(0n, 0n));
  assertEquals(0, wasm.ltu(-123n, 123n));
  assertEquals(1, wasm.ltu(123n, -123n));
  assertEquals(1, wasm.ltu(0x12345678_12488421n, 0x12488421_12345678n));
  assertEquals(0, wasm.ltu(0x12488421_12345678n, 0x12345678_12488421n));
  assertEquals(0, wasm.ltu(0x12345678_87654321n, 0x12345678_87654320n));
  assertEquals(1, wasm.ltu(0x12345678_87654321n, 0x12345678_87654322n));
})();

(function I64LessThanOrEqualUnsigned() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("leu", makeSig([kWasmI64, kWasmI64], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64LeU,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(1, wasm.leu(0n, 0n));
  assertEquals(0, wasm.leu(-123n, 123n));
  assertEquals(1, wasm.leu(123n, -123n));
  assertEquals(1, wasm.leu(0x12345678_12488421n, 0x12488421_12345678n));
  assertEquals(0, wasm.leu(0x12488421_12345678n, 0x12345678_12488421n));
  assertEquals(0, wasm.leu(0x12345678_87654321n, 0x12345678_87654320n));
  assertEquals(1, wasm.leu(0x12345678_87654321n, 0x12345678_87654322n));
})();

(function I64EqualsZero() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("eqz", makeSig([kWasmI64], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64Eqz,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(1, wasm.eqz(0n));
  assertEquals(0, wasm.eqz(1n));
  assertEquals(0, wasm.eqz(-1n));
  assertEquals(0, wasm.eqz(0x100_00000000n));
})();

(function I64Call() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let callee = builder.addFunction("callee",
      makeSig([kWasmI64], [kWasmI64]))
    .addBody([kExprLocalGet, 0])
    .exportFunc();
  builder.addFunction("call", makeSig([kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, callee.index,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(123n, wasm.callee(123n));
  assertEquals(123n, wasm.call(123n));
})();

(function I64CallMultiReturn() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let callee = builder.addFunction("callee",
      makeSig([kWasmI32, kWasmI64, kWasmI64, kWasmI32],
              [kWasmI32, kWasmI64, kWasmI64, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
    ])
    .exportFunc();
  builder.addFunction("call",
      makeSig([kWasmI64, kWasmI64], [kWasmI32, kWasmI64, kWasmI64, kWasmI32]))
    .addBody([
      kExprI32Const, 11,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32Const, 22,
      kExprCallFunction, callee.index,
    ])
    .exportFunc();

  builder.addFunction("tailCall",
      makeSig([kWasmI64, kWasmI64], [kWasmI32, kWasmI64, kWasmI64, kWasmI32]))
    .addBody([
      kExprI32Const, 11,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32Const, 22,
      kExprReturnCall, callee.index,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals([11, 123n, -1n, 22], wasm.callee(11, 123n, -1n, 22));
  assertEquals([11, 123n, -1n, 22], wasm.call(123n, -1n));
  assertEquals([11, 123n, -1n, 22], wasm.tailCall(123n, -1n));

  assertEquals([11, -123n, 123n, 22], wasm.callee(11, -123n, 123n, 22));
  assertEquals([11, -123n, 123n, 22], wasm.call(-123n, 123n));
  assertEquals([11, -123n, 123n, 22], wasm.tailCall(-123n, 123n));
})();

(function I64CountLeadingZeros() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("clz", makeSig([kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64Clz,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0n, wasm.clz(-1n));
  assertEquals(64n, wasm.clz(0n));
  assertEquals(47n, wasm.clz(0x1FFFFn));
})();

(function I64CountTrailingZeros() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("ctz", makeSig([kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64Ctz,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0n, wasm.ctz(-1n));
  assertEquals(64n, wasm.ctz(0n));
  assertEquals(17n, wasm.ctz(0xE0000n));
})();

(function I64PopCount() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("popcnt", makeSig([kWasmI64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64Popcnt,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(64n, wasm.popcnt(-1n));
  assertEquals(0n, wasm.popcnt(0n));
  assertEquals(10n, wasm.popcnt(0b10101011111100001n));
})();

(function I64ConvertFromInt32() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("fromI32", makeSig([kWasmI32], [kWasmI64, kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64SConvertI32,
      kExprLocalGet, 0,
      kExprI64UConvertI32,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals([0n, 0n], wasm.fromI32(0));
  assertEquals([123n, 123n], wasm.fromI32(123));
  assertEquals([-1n, 0xFFFFFFFFn], wasm.fromI32(-1));
  assertEquals([-2147483648n, 2147483648n], wasm.fromI32(0x80000000));
})();

(function I64ConvertFromF64() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("reinterpretF64", makeSig([kWasmF64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64ReinterpretF64
    ])
    .exportFunc();
  builder.addFunction("signedF64", makeSig([kWasmF64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64SConvertF64,
    ])
    .exportFunc();
  builder.addFunction("unsignedF64", makeSig([kWasmF64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64UConvertF64,
    ])
    .exportFunc();
  builder.addFunction("signedSatF64", makeSig([kWasmF64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kNumericPrefix, kExprI64SConvertSatF64,
    ])
    .exportFunc();
  builder.addFunction("unsignedSatF64", makeSig([kWasmF64], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kNumericPrefix, kExprI64UConvertSatF64,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0n, wasm.reinterpretF64(0));
  assertEquals(4638355772470722560n, wasm.reinterpretF64(123));
  assertEquals(-4585016264384053248n, wasm.reinterpretF64(-123));

  assertEquals(0xFF_12345678n, wasm.signedF64(0xFF_12345678));
  assertEquals(-0xFF_12345678n, wasm.signedF64(-0xFF_12345678));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.signedF64(NaN));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.signedF64(Infinity));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.signedF64(-Infinity));

  assertEquals(0xFF_12345678n, wasm.unsignedF64(0xFF_12345678));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.unsignedF64(-1));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.unsignedF64(NaN));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.unsignedF64(Infinity));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.unsignedF64(-Infinity));

  assertEquals(0xFF_12345678n, wasm.signedSatF64(0xFF_12345678));
  assertEquals(-0xFF_12345678n, wasm.signedSatF64(-0xFF_12345678));
  assertEquals(0n, wasm.signedSatF64(NaN));
  assertEquals(9223372036854775807n, wasm.signedSatF64(Infinity));
  assertEquals(-9223372036854775808n, wasm.signedSatF64(-Infinity));

  assertEquals(0xFF_12345678n, wasm.unsignedSatF64(0xFF_12345678));
  assertEquals(0n, wasm.unsignedSatF64(-0xFF_12345678));
  assertEquals(0n, wasm.unsignedSatF64(NaN));
  assertEquals(-1n, wasm.unsignedSatF64(Infinity));
  assertEquals(0n, wasm.unsignedSatF64(-Infinity));
})();

(function I64ConvertFromF32() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("signedF32", makeSig([kWasmF32], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64SConvertF32,
    ])
    .exportFunc();
  builder.addFunction("unsignedF32", makeSig([kWasmF32], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64UConvertF32,
    ])
    .exportFunc();
  builder.addFunction("signedSatF32", makeSig([kWasmF32], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kNumericPrefix, kExprI64SConvertSatF32,
    ])
    .exportFunc();
  builder.addFunction("unsignedSatF32", makeSig([kWasmF32], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kNumericPrefix, kExprI64UConvertSatF32,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  // Loss of precision due to float32.
  assertEquals(0xFF_12340000n, wasm.signedF32(0xFF_12345678));
  assertEquals(-0xFF_12340000n, wasm.signedF32(-0xFF_12345678));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.signedF32(NaN));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.signedF32(Infinity));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.signedF32(-Infinity));

  assertEquals(0xFF_12340000n, wasm.unsignedF32(0xFF_12345678));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.unsignedF32(-1));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.unsignedF32(NaN));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.unsignedF32(Infinity));
  assertTraps(kTrapFloatUnrepresentable, () => wasm.unsignedF32(-Infinity));

  assertEquals(0xFF_12340000n, wasm.signedSatF32(0xFF_12345678));
  assertEquals(-0xFF_12340000n, wasm.signedSatF32(-0xFF_12345678));
  assertEquals(0n, wasm.signedSatF32(NaN));
  assertEquals(9223372036854775807n, wasm.signedSatF32(Infinity));
  assertEquals(-9223372036854775808n, wasm.signedSatF32(-Infinity));

  assertEquals(0xFF_12340000n, wasm.unsignedSatF32(0xFF_12345678));
  assertEquals(0n, wasm.unsignedSatF32(-0xFF_12345678));
  assertEquals(0n, wasm.unsignedSatF32(NaN));
  assertEquals(-1n, wasm.unsignedSatF32(Infinity));
  assertEquals(0n, wasm.unsignedSatF32(-Infinity));
})();
