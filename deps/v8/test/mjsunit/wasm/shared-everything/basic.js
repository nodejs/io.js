// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-shared

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function SharedGlobal() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_i_i, kNoSuperType, true, true);
  let global = builder.addGlobal(kWasmI32, true, true, [kExprI32Const, 0]);

  builder.addFunction("roundtrip", sig)
    .addBody([kExprLocalGet, 0, kExprGlobalSet, global.index,
              kExprGlobalGet, global.index])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(10, wasm.roundtrip(10));
})();

(function SharedGlobalAbstractType() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_i_i, kNoSuperType, true, false);
  let struct = builder.addStruct(
    [makeField(kWasmI32, true)], kNoSuperType, false, true);
  let global = builder.addGlobal(
    wasmRefNullType(kWasmAnyRef, true), true, true,
    [kExprRefNull, kWasmSharedTypeForm, kAnyRefCode]);

  builder.addFunction("roundtrip", sig)
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprStructNew, struct,
              kExprGlobalSet, global.index,
              kExprGlobalGet, global.index,
              kGCPrefix, kExprRefCast, struct,
              kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(10, wasm.roundtrip(10));
})();

(function SharedGlobalInNonSharedFunction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_i_i, kNoSuperType, true, false);
  let global = builder.addGlobal(kWasmI32, true, true, [kExprI32Const, 0]);

  builder.addFunction("roundtrip", sig)
    .addBody([kExprLocalGet, 0, kExprGlobalSet, global.index,
              kExprGlobalGet, global.index])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  assertEquals(10, wasm.roundtrip(10));
})();

(function InvalidGlobalInSharedFunction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_v_i, kNoSuperType, true, true);
  let global = builder.addGlobal(kWasmI32, true, false, [kExprI32Const, 0]);

  builder.addFunction("setter", sig)
    .addBody([kExprLocalGet, 0, kExprGlobalSet, global.index]);

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /Cannot access non-shared global 0 in a shared function/);
})();

(function InvalidGlobalInSharedConstantExpression() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let global_non_shared =
      builder.addGlobal(kWasmI32, true, false, [kExprI32Const, 0]);
  builder.addGlobal(
      kWasmI32, true, true, [kExprGlobalGet, global_non_shared.index]);

  assertThrows(
      () => builder.instantiate(), WebAssembly.CompileError,
      /Cannot access non-shared global 0 in a shared constant expression/);
})();

(function InvalidImportedGlobal() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_i_i);
  builder.addImportedGlobal("m", "g", wasmRefType(sig), true, true);

  assertThrows(
    () => builder.instantiate(), WebAssembly.CompileError,
    /shared imported global must have shared type/);
})();

(function SharedTypes() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_i_i, kNoSuperType, true, true);
  let struct =
    builder.addStruct([makeField(kWasmI32, true)], kNoSuperType, false, true);

  builder.addFunction("main", sig)
    .addLocals(wasmRefType(struct), 1)
    .addBody([
      kExprLocalGet, 0, kGCPrefix, kExprStructNew, struct,
      kExprLocalSet, 1,
      kExprLocalGet, 1, kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(42, wasm.main(42));
})();

(function InvalidLocal() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_i_i, kNoSuperType, true, true);
  let struct =
    builder.addStruct([makeField(kWasmI32, true)], kNoSuperType, false, false);

  builder.addFunction("main", sig)
    .addLocals(wasmRefType(struct), 1)
    .addBody([kExprLocalGet, 0])
    .exportFunc();

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /local must have shared type/)
})();

(function InvalidStack() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_i_v, kNoSuperType, true, true);
  let struct =
    builder.addStruct([makeField(kWasmI32, true)], kNoSuperType, false, false);

  builder.addFunction("main", sig)
    .addBody([
      kExprI32Const, 42, kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /struct.new does not have a shared type/);
})();

(function InvalidFuncRefInConstantExpression() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_i_i);
  let adder = builder.addFunction("adder", sig)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
  let global = builder.addGlobal(wasmRefType(sig), true, true,
                                 [kExprRefFunc, adder.index]);

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /ref.func does not have a shared type/);
})();

(function DataSegmentInFunction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_v_v, kNoSuperType, false, true);
  let data = builder.addPassiveDataSegment([1, 2, 3], true);
  builder.addFunction("drop", sig)
    .addBody([kNumericPrefix, kExprDataDrop, data])
  builder.instantiate();
})();

(function InvalidDataSegmentInFunction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_v_v, kNoSuperType, false, true);
  let data = builder.addPassiveDataSegment([1, 2, 3], false);
  builder.addFunction("drop", sig)
    .addBody([kNumericPrefix, kExprDataDrop, data])

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /cannot refer to non-shared segment/);
})();

(function ElementSegmentInFunction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_v_v, kNoSuperType, false, true);
  let func = builder.addFunction("void", sig).addBody([]);
  let elem = builder.addPassiveElementSegment(
    [[kExprRefFunc, func.index]], wasmRefType(0), true);
  builder.addFunction("drop", sig)
    .addBody([kNumericPrefix, kExprElemDrop, elem])
  builder.instantiate();
})();

(function InvalidElementSegmentInFunction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_v_v, kNoSuperType, false, true);
  let elem = builder.addPassiveElementSegment(
    [[kExprRefNull, kNullFuncRefCode]], kWasmFuncRef, false);
  builder.addFunction("drop", sig)
    .addBody([kNumericPrefix, kExprElemDrop, elem])

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /cannot reference non-shared element segment/);
})();

(function TableInFunction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)], kNoSuperType,
                                 true, true);
  let sig = builder.addType(makeSig([kWasmI32], [wasmRefNullType(struct)]),
                            kNoSuperType, false, true);
  let table = builder.addTable(wasmRefNullType(struct), 10, undefined,
                               [kExprRefNull, struct], true);
  builder.addFunction("get", sig)
    .addBody([kExprLocalGet, 0, kExprTableGet, table.index])
    .exportFunc();

  let instance = builder.instantiate();

  assertEquals(null, instance.exports.get(0));
})();

(function InvalidTableInFunction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)], kNoSuperType,
                                 true, true);
  let sig = builder.addType(makeSig([kWasmI32], [wasmRefNullType(struct)]),
                            kNoSuperType, false, true);
  let table = builder.addTable(wasmRefNullType(struct), 10, undefined,
                               [kExprRefNull, struct], false);
  builder.addFunction("get", sig)
    .addBody([kExprLocalGet, 0, kExprTableGet, table.index])
    .exportFunc();

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /cannot reference non-shared table/);
})();

(function InvalidTableInitializer() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_shared = builder.addType(kSig_v_v, kNoSuperType, false, true);
  let sig = builder.addType(kSig_v_v, kNoSuperType, false, false);
  builder.addTable(wasmRefNullType(sig_shared), 10, undefined,
                   [kExprRefNull, sig], true);

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /type error in constant expression\[0\] \(expected \(ref null 0\), got \(ref null 1\)\)/);
})();
