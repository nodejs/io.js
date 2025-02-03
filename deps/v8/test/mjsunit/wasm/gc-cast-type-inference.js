// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestRefCastNullReturnsNullTypeForNonNullInput() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let consumeRefI31 =
    builder.addFunction(`consumeRefI31`,
                        makeSig([wasmRefType(kWasmI31Ref)], []))
    .addBody([]);

  builder.addFunction(`refCastRemovesNullability`,
                      makeSig([kWasmExternRef], []))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprAnyConvertExtern,
    kExprRefAsNonNull,
    kGCPrefix, kExprRefCastNull, kI31RefCode,
    // ref.cast null pushes a nullable value on the stack even though its input
    // was non-nullable, therefore this call is not spec-compliant.
    kExprCallFunction, consumeRefI31.index,
  ]).exportFunc();

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /expected type \(ref i31\), found .* type i31ref/);
})();

(function TestRefCastRemovesNullability() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let i31ToI32 =
    builder.addFunction(`i31ToI32`,
                        makeSig([wasmRefType(kWasmI31Ref)], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprI31GetS
    ]);

  builder.addFunction(`refCastRemovesNullability`,
                      makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprAnyConvertExtern,
    kGCPrefix, kExprRefCast, kI31RefCode,
    // ref.cast pushes a non-nullable value on the stack even for a nullable
    // input value as the instruction traps on null.
    kExprCallFunction, i31ToI32.index,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(42, wasm.refCastRemovesNullability(42));
  assertEquals(-1, wasm.refCastRemovesNullability(-1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastRemovesNullability(null));
})();

(function TestBrOnCastNullability() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let consumeNonNull =
    builder.addFunction(`consumeNonNull`,
                        makeSig([wasmRefType(kWasmAnyRef)], []))
    .addBody([]);
  let i31ToI32 =
    builder.addFunction(`i31ToI32`,
                        makeSig([wasmRefType(kWasmI31Ref)], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprI31GetS]);


  builder.addFunction(`brOnCastNullNonNullOnPassThrough`,
                      makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprBlock, kWasmRefNull, kI31RefCode,
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprBrOnCast, 0b11, 0, kAnyRefCode, kI31RefCode,
      // As null branches, the type here is guaranteed to be non-null.
      kExprCallFunction, consumeNonNull.index,
      kExprI32Const, 0,
      kExprReturn,
    kExprEnd,
    kGCPrefix, kExprI31GetS,
    kExprReturn,
  ]).exportFunc();

  builder.addFunction(`brOnCastNonNullOnBranch`,
                      makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    // The value is guaranteed to be non-null on branch.
    kExprBlock, kWasmRef, kI31RefCode,
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprBrOnCast, 0b01, 0, kAnyRefCode, kI31RefCode,
      kExprDrop,
      kExprI32Const, 0,
      kExprReturn,
    kExprEnd,
    kExprCallFunction, i31ToI32.index,
    kExprReturn,
  ]).exportFunc();

  let instance = builder.instantiate();
  let wasm = instance.exports;
  assertTraps(kTrapNullDereference, () => wasm.brOnCastNullNonNullOnPassThrough(null));
  assertEquals(42, wasm.brOnCastNullNonNullOnPassThrough(42));
  assertEquals(0, wasm.brOnCastNullNonNullOnPassThrough("cast fails"));
  assertEquals(0, wasm.brOnCastNonNullOnBranch(null));
  assertEquals(42, wasm.brOnCastNonNullOnBranch(42));
  assertEquals(0, wasm.brOnCastNonNullOnBranch("cast fails"));
})();

(function TestBrOnCastFailNullability() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let consumeNonNull =
    builder.addFunction(`consumeNonNull`,
                        makeSig([wasmRefType(kWasmAnyRef)], []))
    .addBody([]);
  let i31ToI32 =
    builder.addFunction(`i31ToI32`,
                        makeSig([wasmRefType(kWasmI31Ref)], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprI31GetS]);

  builder.addFunction(`brOnCastFailNonNullOnPassThrough`,
                      makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprBlock, kWasmRefNull, kAnyRefCode,
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprBrOnCastFail, 0b01, 0, kAnyRefCode, kI31RefCode,
      // As null branches, the type here is guaranteed to be non-null.
      kExprCallFunction, i31ToI32.index,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 1,
    kExprReturn,
  ]).exportFunc();

  builder.addFunction(`brOnCastFailNullNonNullOnBranch`,
                      makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    // The value is guaranteed to be non-null on branch.
    kExprBlock, kWasmRef, kAnyRefCode,
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprBrOnCastFail, 0b11, 0, kAnyRefCode, kI31RefCode,
      kGCPrefix, kExprI31GetS,
      kExprReturn,
    kExprEnd,
    kExprCallFunction, consumeNonNull.index,
    kExprI32Const, 1,
    kExprReturn,
  ]).exportFunc();

  let instance = builder.instantiate();
  let wasm = instance.exports;
  assertEquals(1, wasm.brOnCastFailNonNullOnPassThrough(null));
  assertEquals(42, wasm.brOnCastFailNonNullOnPassThrough(42));
  assertEquals(1, wasm.brOnCastFailNonNullOnPassThrough("cast fails"));
  assertTraps(kTrapNullDereference, () => wasm.brOnCastFailNullNonNullOnBranch(null));
  assertEquals(42, wasm.brOnCastFailNullNonNullOnBranch(42));
  assertEquals(1, wasm.brOnCastFailNullNonNullOnBranch("cast fails"));
})();
