// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-wasm-lazy-compilation
// Flags: --turboshaft-wasm --enable-testing-opcode-in-wasm

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Make sure turboshaft bails out graciously for non-implemented features.
(function Bailout() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("bailout", makeSig([], []))
    .addBody([kExprNopForTestingUnsupportedInLiftoff])
    .exportFunc();

  builder.instantiate();
})();

(function I32Arithmetic() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("i32_arithmetic", kSig_i_v)
    .addBody([kExprI32Const, 32, kExprI32Const, 10, kExprI32Add])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(42, wasm.i32_arithmetic());
})();

(function I32Locals() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("i32_locals", kSig_i_ii)
    .addLocals(kWasmI32, 1)
    .addBody([
      // i = i + 1;
      kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add, kExprLocalSet, 0,
      // k = j + 2; temp1 = k;
      kExprLocalGet, 1, kExprI32Const, 2, kExprI32Add, kExprLocalTee, 2,
      // temp2 = temp1 + i;
      kExprLocalGet, 0, kExprI32Add,
      // return temp2 * k;
      kExprLocalGet, 2, kExprI32Mul])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals((22 + 11) * 22, wasm.i32_locals(10, 20));
})();

(function IfThenElse() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let if_sig_32 = builder.addType(kSig_ii_ii);

  builder.addFunction("if_then_else_32", kSig_i_iii)
    .addBody([
      // f(x, y, z) {
      //   [temp1, temp2] = z ? [x + y, x] : [x - y, y];
      //   return temp1 * temp2;
      // }
      kExprLocalGet, 0, kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprIf, if_sig_32,
        kExprI32Add, kExprLocalGet, 0,
      kExprElse,
        kExprI32Sub, kExprLocalGet, 1,
      kExprEnd,
      kExprI32Mul])
    .exportFunc();

  let if_sig_64 = builder.addType(
      makeSig([kWasmI64, kWasmI64], [kWasmI64, kWasmI64]));

  builder.addFunction(
      "if_then_else_64",
      makeSig([kWasmI64, kWasmI64, kWasmI32], [kWasmI64]))
    .addBody([
      // f(x, y, z) {
      //   [temp1, temp2] = z ? [x + y, x] : [x - y, y];
      //   return temp1 * temp2;
      // }
      kExprLocalGet, 0, kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprIf, if_sig_64,
        kExprI64Add, kExprLocalGet, 0,
      kExprElse,
        kExprI64Sub, kExprLocalGet, 1,
      kExprEnd,
      kExprI64Mul])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(-200, wasm.if_then_else_32(10, 20, 0));
  assertEquals(300, wasm.if_then_else_32(10, 20, 1));
  assertEquals(-200n, wasm.if_then_else_64(10n, 20n, 0));
  assertEquals(300n, wasm.if_then_else_64(10n, 20n, 1));
})();

(function OneArmedIf() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addFunction("one_armed_if", kSig_i_ii)
    .addBody([
      // f(x, y) {
      //   if (y) x = x + 1;
      //   return x;
      // }
      kExprLocalGet, 1,
      kExprIf, kWasmVoid,
        kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add, kExprLocalSet, 0,
      kExprEnd,
      kExprLocalGet, 0])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(10, wasm.one_armed_if(10, 0));
  assertEquals(11, wasm.one_armed_if(10, -1));
})();

(function BlockAndBr() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let block_sig = builder.addType(kSig_ii_ii);

  builder.addFunction("block_and_br", kSig_i_iii)
    .addBody([
      // f(x, y, z) {
      //   temp1 = x + y;
      //   temp2 = x;
      //   if (z) goto block;
      //   temp2 = temp2 * y;
      //  block:
      //   return temp1 + temp2;
      // }
      kExprLocalGet, 0, kExprLocalGet, 1,
      kExprBlock, block_sig,
        kExprI32Add,
        kExprLocalGet, 0,
        kExprLocalGet, 2,
        kExprBrIf, 0,
        kExprLocalGet, 1,
        kExprI32Mul,
      kExprEnd,
      kExprI32Add])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals((10 + 20) + (10 * 20), wasm.block_and_br(10, 20, 0));
  assertEquals((10 + 20) + 10, wasm.block_and_br(10, 20, 1));
})();

(function Loop() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let loop_sig = builder.addType(kSig_i_i);

  // Works for positive numbers only.
  builder.addFunction("factorial", kSig_i_i)
    .addBody([
      kExprI32Const, 1,
      kExprLoop, loop_sig,
        kExprLocalGet, 0,
        kExprI32Mul,
        kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub, kExprLocalSet, 0,
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kExprI32GtS,
        kExprBrIf, 0,
      kExprEnd])
    .exportFunc();

  builder.addFunction("factorial_with_br_if_return", kSig_i_i)
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprI32Const, 1, kExprLocalSet, 1,
      kExprLoop, kWasmVoid,
        kExprLocalGet, 1,
        kExprLocalGet, 0, kExprI32Const, 1, kExprI32LtS,
        kExprBrIf, 1,
        kExprLocalGet, 0, kExprI32Mul, kExprLocalSet, 1,
        kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub, kExprLocalSet, 0,
        kExprBr, 0,
      kExprEnd,
      kExprUnreachable])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(1, wasm.factorial(1));
  assertEquals(24, wasm.factorial(4));
  assertEquals(720, wasm.factorial(6));

  assertEquals(1, wasm.factorial_with_br_if_return(0));
  assertEquals(1, wasm.factorial_with_br_if_return(1));
  assertEquals(24, wasm.factorial_with_br_if_return(4));
  assertEquals(720, wasm.factorial_with_br_if_return(6));
})();

(function Multireturn() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addFunction("swap", kSig_ii_ii)
    .addBody([kExprI32Const, 42, // garbage
              kExprLocalGet, 1, kExprLocalGet, 0, kExprReturn])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals([1, 0], wasm.swap(0, 1));
})();

(function BrTable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let block_sig = builder.addType(kSig_i_i);

  builder.addFunction("br_table", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprBlock, block_sig,
        kExprBlock, block_sig,
          kExprLocalGet, 0,
          kExprBrTable, 2, 2, 1, 0,
        kExprEnd,
        kExprI32Const, 1, kExprI32Add, kExprReturn,
      kExprEnd,
      kExprI32Const, 2, kExprI32Sub])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(0, wasm.br_table(0));
  assertEquals(-1, wasm.br_table(1));
  assertEquals(23, wasm.br_table(22));
})();

(function TestSubZeroFromTruncatedOptimization() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("subZeroFromTruncated", makeSig([kWasmI64], [kWasmI32]))
    .exportFunc()
    .addBody([
      // i32.sub(i32.wrap_i64(local.get 0), 0)
      // should be optimized to i32.wrap_i64(local.get 0)
      kExprLocalGet, 0,
      kExprI32ConvertI64,
      kExprI32Const, 0,
      kExprI32Sub,
    ]);
  let instance = builder.instantiate();
  assertEquals(123, instance.exports.subZeroFromTruncated(123n));
})();
