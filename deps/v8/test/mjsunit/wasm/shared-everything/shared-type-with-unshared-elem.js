// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(manoskouk): Revisit flags once shared everything threads doesn't
// conflict with --liftoff any more.
// Flags: --experimental-wasm-shared --no-liftoff --turbofan

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestSharedTableWithUnsharedElementType() {
  const builder = new WasmModuleBuilder();
  builder.addTable(kWasmFuncRef, 0, undefined, undefined, /*is_shared*/ true);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /Shared table 0 must have shared element type, actual type funcref/);
})();

(function TestSharedGlobalWithUnsharedElementType() {
  const builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmFuncRef, false, /*is_shared*/ true);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /Shared global 0 must have shared type, actual type funcref/);
})();

(function TestImportedSharedTableWithUnsharedElementType() {
  const builder = new WasmModuleBuilder();
  builder.addImportedTable("import", "table", 0, 10, kWasmFuncRef, /*is_shared*/ true);
  assertThrows(() => builder.toModule(), WebAssembly.CompileError,
    /Shared table 0 must have shared element type, actual type funcref/);
})();

(function TestImportedSharedGlobalWithUnsharedElementType() {
  const builder = new WasmModuleBuilder();
  builder.addImportedGlobal("import", "global", kWasmFuncRef, true, /*is_shared*/ true);
  assertThrows(() => builder.toModule(), WebAssembly.CompileError,
    /shared imported global must have shared type/);
})();


(function TestSharedArrayWithUnsharedElement() {
  const builder = new WasmModuleBuilder();
  builder.addArray(kWasmFuncRef, false, kNoSuperType, false, /*is_shared*/ true);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /Shared array type must have shared element type, actual type funcref/);
})();

(function TestSharedStructWithUnsharedField() {
  const builder = new WasmModuleBuilder();
  builder.addStruct(
    [makeField(kWasmI32, false), makeField(kWasmFuncRef, false)],
    kNoSuperType, false, /*is_shared*/ true);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /Shared struct type must have shared field types, actual type funcref for field 1/);
})();

(function TestSharedSignatureWithUnsharedParameter() {
  const builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmFuncRef], []);
  builder.addType(sig, kNoSuperType, false, /*is_shared*/ true);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /Shared signature types must have shared parameter types, actual type funcref for parameter 0/);
})();

(function TestSharedSignatureWithUnsharedReturn() {
  const builder = new WasmModuleBuilder();
  let sig = makeSig([], [kWasmFuncRef]);
  builder.addType(sig, kNoSuperType, false, /*is_shared*/ true);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /Shared signature types must have shared return types, actual type funcref for return 0/);
})();
