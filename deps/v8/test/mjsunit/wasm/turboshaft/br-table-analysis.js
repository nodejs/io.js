// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-wasm-lazy-compilation
// Flags: --turboshaft-wasm --enable-testing-opcode-in-wasm

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function BrTablePrimaryIsDefault() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let block_sig = builder.addType(kSig_i_i);

  builder.addFunction("br_table", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprBlock, block_sig,
        kExprBlock, block_sig,
          kExprLocalGet, 0,
          kExprBrTable, 4, 2, 0, 1, 0, 1, 0,
        kExprEnd,
        kExprI32Const, 1, kExprI32Add, kExprReturn,
      kExprEnd,
      kExprI32Const, 2, kExprI32Sub])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(0, wasm.br_table(0));
  assertEquals(2, wasm.br_table(1));
  assertEquals(0, wasm.br_table(2));
  assertEquals(4, wasm.br_table(3));
  assertEquals(2, wasm.br_table(4));
  assertEquals(3, wasm.br_table(5));
  assertEquals(8, wasm.br_table(10));
})();

(function BrTablePrimaryIsNotDefault() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let block_sig = builder.addType(kSig_i_i);

  builder.addFunction("br_table", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprBlock, block_sig,
        kExprBlock, block_sig,
          kExprLocalGet, 0,
          kExprBrTable, 5, 2, 1, 1, 1, 1, 0,
        kExprEnd,
        kExprI32Const, 1, kExprI32Add, kExprReturn,
      kExprEnd,
      kExprI32Const, 2, kExprI32Sub])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(0, wasm.br_table(0));
  assertEquals(-1, wasm.br_table(1));
  assertEquals(0, wasm.br_table(2));
  assertEquals(1, wasm.br_table(3));
  assertEquals(2, wasm.br_table(4));
  assertEquals(6, wasm.br_table(5));
  assertEquals(7, wasm.br_table(6));
  assertEquals(11, wasm.br_table(10));
})();

(function BrTablePrimaryIsNotDefaultTwoOrOthers() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let block_sig = builder.addType(kSig_i_i);

  builder.addFunction("br_table", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprBlock, block_sig,
        kExprBlock, block_sig,
          kExprLocalGet, 0,
          kExprBrTable, 6, 2, 1, 0, 0, 0, 2, 1,
        kExprEnd,
        kExprI32Const, 1, kExprI32Add, kExprReturn,
      kExprEnd,
      kExprI32Const, 2, kExprI32Sub])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(0, wasm.br_table(0));
  assertEquals(-1, wasm.br_table(1));
  assertEquals(3, wasm.br_table(2));
  assertEquals(4, wasm.br_table(3));
  assertEquals(5, wasm.br_table(4));
  assertEquals(5, wasm.br_table(5));
  assertEquals(4, wasm.br_table(6));
  assertEquals(8, wasm.br_table(10));
})();
