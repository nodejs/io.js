// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff
// Needed for exceptions-utils.js.
// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");
d8.file.execute("test/mjsunit/wasm/exceptions-utils.js");

// Test that lowering a ror operator with int64-lowering does not produce
// floating control, which is incompatible with loop unrolling.
(function I64RorLoweringTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory(1000, 1000);

  builder.addFunction("main", makeSig([kWasmI32, kWasmI64], []))
    .addBody([
      kExprLoop, kWasmVoid,
        kExprLocalGet, 0x00,
        kExprI32LoadMem, 0x00, 0x00,
        kExprI64UConvertI32,
        kExprLocalGet, 0x01,
        kExprI64Ror,
        kExprI32ConvertI64,
        kExprBrIf, 0x00,
      kExprEnd])
    .exportFunc();

  let module = new WebAssembly.Module(builder.toBuffer());
  new WebAssembly.Instance(module);
})();

// Test the interaction between multireturn and loop unrolling.
(function MultiBlockResultTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_ii_ii);

  // f(a, b) = a + b + b + b - a*b*b*b
  builder.addFunction("main", kSig_i_ii)
    .addLocals(kWasmI32, 2)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 0,
      kExprLoop, sig,
        kExprLocalSet, 2,  // Temporarily store the second value.
        kExprLocalGet, 1, kExprI32Add,
        // multiply the second value by 2
        kExprLocalGet, 2, kExprLocalGet, 1, kExprI32Mul,
        // Increment counter, then loop if <= 3.
        kExprLocalGet, 3, kExprI32Const, 1, kExprI32Add, kExprLocalSet, 3,
        kExprLocalGet, 3, kExprI32Const, 3, kExprI32LtS,
        kExprBrIf, 0,
      kExprEnd,
      kExprI32Sub])
    .exportFunc();

  let instance = builder.instantiate();
  assertEquals(10 + 5 + 5 + 5 - (10 * 5 * 5 * 5), instance.exports.main(10, 5))
})();

// Test the interaction between tail calls and loop unrolling.
(function TailCallTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprLocalGet, 0]);

  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprLocalGet, 0,
        kExprIf, kWasmVoid,
          kExprLocalGet, 0,
          kExprReturnCall, callee.index,
        kExprElse,
          kExprBr, 1,
        kExprEnd,
      kExprEnd,
      kExprUnreachable
    ])
    .exportAs("main");

  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module);
  assertEquals(instance.exports.main(1), 1);
})();

// Test the interaction between the eh proposal and loop unrolling.

(function TestRethrowNested() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except1 = builder.addTag(kSig_v_v);
  let except2 = builder.addTag(kSig_v_v);
  builder.addFunction("rethrow_nested", kSig_i_i)
    .addBody([
      kExprLoop, kWasmI32,
      kExprTry, kWasmI32,
        kExprLoop, kWasmI32,
          kExprThrow, except2,
        kExprEnd,
      kExprCatch, except2,
        kExprTry, kWasmI32,
          kExprThrow, except1,
        kExprCatch, except1,
          kExprLocalGet, 0,
          kExprI32Const, 0,
          kExprI32Eq,
          kExprIf, kWasmVoid,
            kExprLoop, kWasmVoid,
              kExprRethrow, 2,
            kExprEnd,
          kExprEnd,
          kExprLocalGet, 0,
          kExprI32Const, 1,
          kExprI32Eq,
          kExprIf, kWasmVoid,
            kExprLoop, kWasmVoid,
              kExprRethrow, 3,
            kExprEnd,
          kExprEnd,
          kExprI32Const, 23,
        kExprEnd,
      kExprEnd,
      kExprEnd])
    .exportFunc();
  let instance = builder.instantiate();

  assertWasmThrows(instance, except1, [],
                   () => instance.exports.rethrow_nested(0));
  assertWasmThrows(instance, except2, [],
                   () => instance.exports.rethrow_nested(1));
  assertEquals(23, instance.exports.rethrow_nested(2));
})();

(function TestThrow() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except1 = builder.addTag(kSig_v_v);
  builder.addFunction("throw", kSig_i_i)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprLocalGet, 0,
        kExprI32Const, 10,
        kExprI32GtS,
        kExprIf, kWasmVoid,
          kExprThrow, except1,
        kExprElse,
          kExprLocalGet, 0,
          kExprI32Const, 1,
          kExprI32Add,
          kExprLocalSet, 0,
          kExprBr, 1,
        kExprEnd,
      kExprEnd,
      kExprLocalGet, 0
    ])
    .exportFunc();

  let instance = builder.instantiate();
  assertWasmThrows(instance, except1, [], ()=>instance.exports.throw(0));
})();

(function TestThrowCatch() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except1 = builder.addTag(kSig_v_v);
  builder.addFunction("throw_catch", kSig_i_i)
    .addBody([
      kExprLoop, kWasmI32,
        kExprTry, kWasmI32,
          kExprLocalGet, 0,
          kExprI32Const, 10,
          kExprI32GtS,
          kExprIf, kWasmVoid,
            kExprThrow, except1,
          kExprElse,
            kExprLocalGet, 0,
            kExprI32Const, 1,
            kExprI32Add,
            kExprLocalSet, 0,
            kExprBr, 2,
          kExprEnd,
          kExprLocalGet, 0,
        kExprCatch, except1,
          kExprLocalGet, 0,
        kExprEnd,
      kExprEnd])
    .exportFunc();

  let instance = builder.instantiate();
  assertEquals(11, instance.exports.throw_catch(0));
})();

// Test that loops are unrolled in the presence of builtins.
(function UnrollWithBuiltinsTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addTable(kWasmFuncRef, 10, 10);

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add])
    .exportFunc();

  builder.addFunction("main", makeSig([kWasmI32], []))
    .addBody([
      kExprLoop, kWasmVoid,
        kExprLocalGet, 0, kExprI32Const, 0, kExprI32LtS, kExprBrIf, 1,
        kExprLocalGet, 0,
        kExprRefFunc, callee.index,
        kExprTableSet, 0,
        kExprBr, 0,
      kExprEnd])
    .exportFunc();

  builder.instantiate();
})();

// Test that loops are *not* unrolled in the presence of direct/indirect calls.
(function LoopWithCallsTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add])
    .exportFunc();

  builder.addFunction("main", makeSig([kWasmI32], []))
    .addBody([
      kExprLoop, kWasmVoid,
        kExprLocalGet, 0,
        kExprRefFunc, callee.index,
        kExprCallRef, callee.type_index,
        kExprBrIf, 0,
      kExprEnd,
      kExprLoop, kWasmVoid,
        kExprLocalGet, 0,
        kExprCallFunction, callee.index,
        kExprBrIf, 0,
      kExprEnd])
    .exportFunc();

  builder.instantiate();
})();
