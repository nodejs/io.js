// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-exnref

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestRefTestInvalid() {
  print(arguments.callee.name);
  let struct = 0;
  let array = 1;
  let sig = 2;
  let types = [
    // source value type |target heap type
    [kWasmI32,            kAnyRefCode],
    [kWasmNullExternRef,  struct],
    [wasmRefType(struct), kNullFuncRefCode],
    [wasmRefType(array),  kFuncRefCode],
    [wasmRefType(struct), sig],
    [wasmRefType(sig),    struct],
    [wasmRefType(sig),    kExternRefCode],
    [kWasmAnyRef,         kExternRefCode],
    [kWasmAnyRef,         kFuncRefCode],
    [kWasmAnyRef,         kExnRefCode],
    [wasmRefType(sig),    kExnRefCode],
    [kWasmNullExternRef,  kExnRefCode],
    [wasmRefType(array),  kNullExnRefCode],
    [kWasmNullFuncRef,    kNullExnRefCode],
  ];
  let casts = [
    kExprRefTest,
    kExprRefTestNull,
    kExprRefCast,
    kExprRefCastNull,
  ];

  for (let [source_type, target_type_imm] of types) {
    for (let cast of casts) {
      let builder = new WasmModuleBuilder();
      assertEquals(struct, builder.addStruct([makeField(kWasmI32, true)]));
      assertEquals(array, builder.addArray(kWasmI32));
      assertEquals(sig, builder.addType(makeSig([kWasmI32], [])));
      builder.addFunction('refTest', makeSig([source_type], []))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, cast, target_type_imm,
        kExprDrop,
      ]);

      assertThrows(() => builder.instantiate(),
                  WebAssembly.CompileError,
                  /has to be in the same reference type hierarchy/);
    }
  }
})();

(function TestBrOnCastExpectsNonNull() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  builder.addFunction('fct', makeSig([wasmRefNullType(struct)], []))
  .addBody([
    kExprBlock, kAnyRefCode,
      kExprLocalGet, 0,
      ...wasmBrOnCast(0, wasmRefType(struct), wasmRefType(struct)),
      kExprDrop,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprReturn,
  ]);

  assertThrows(() => builder.instantiate(),
    WebAssembly.CompileError,
    /br_on_cast\[0\] expected type \(ref 0\), found local.get of type \(ref null 0\)/);
})();

(function TestBrOnCastExpectsNull() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  builder.addFunction('fct', makeSig([wasmRefType(struct)], []))
  .addBody([
    kExprBlock, kAnyRefCode,
      kExprLocalGet, 0,
      ...wasmBrOnCast(0, wasmRefNullType(struct), wasmRefNullType(struct)),
      kExprDrop,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprReturn,
  ]);
  // Specifying a nullable source and providing a non-nullable value is not an
  // error (as non-nullable values can be cast implicitly to nullable).
  builder.instantiate();
})();

(function TestBrOnCastNonNullToNull() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  builder.addFunction('fct', makeSig([wasmRefType(struct)], []))
  .addBody([
    kExprBlock, kAnyRefCode,
      kExprLocalGet, 0,
      ...wasmBrOnCast(0, wasmRefType(struct), wasmRefNullType(struct)),
      kExprDrop,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprReturn,
  ]);
  // Even though the source is non-nullable, if the br_on_cast is set to produce
  // a nullable value on cast, the label target must be nullable as well.
  assertThrows(() => builder.instantiate(),
    WebAssembly.CompileError,
    /invalid types for br_on_cast: \(ref null 0\) is not a subtype of \(ref 0\)/);
})();

(function TestBrOnCastInvalidFlags() {
  print(arguments.callee.name);
  for (let value of [-1, 8, 127, 255, 256]) {
    let builder = new WasmModuleBuilder();
    let struct = builder.addStruct([makeField(kWasmI32, true)]);
    builder.addFunction('fct', makeSig([wasmRefType(struct)], []))
    .addBody([
      kExprBlock, kAnyRefCode,
        kExprLocalGet, 0,
        kGCPrefix, kExprBrOnCast,
          ...wasmUnsignedLeb(value), 0, kAnyRefCode, struct,
        kExprDrop,
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprReturn,
    ]);
    assertThrows(() => builder.instantiate(),
      WebAssembly.CompileError,
      /invalid br_on_cast flags [0-9]+/);
  }
})();

(function TestBrOnCastSourceTypeUpcast() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('fct', makeSig([wasmRefType(struct)], []))
  .addBody([
    kExprBlock, kAnyRefCode,
      kExprLocalGet, 0,
      ...wasmBrOnCast(0, wasmRefType(kWasmAnyRef), wasmRefType(struct)),
      kGCPrefix, kExprStructGet, struct, 0,
      kExprDrop,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprReturn,
  ]);
  // While the value stack type is (ref struct), the source type in the
  // br_on_cast is specified as (ref any), so the fallthrough type becomes
  // the less specific (ref any).
  assertThrows(() => builder.instantiate(),
    WebAssembly.CompileError,
    /struct.get\[0\] expected type \(ref null 0\), found local.get of type \(ref any\)/);
})();

(function TestBrOnCastFailSourceTypeUpcast() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let structRefSig =
      builder.addType(makeSig([], [wasmRefType(struct)]));

  builder.addFunction('fct', makeSig([wasmRefType(struct)], []))
  .addBody([
    kExprBlock, structRefSig,
      kExprLocalGet, 0,
      ...wasmBrOnCastFail(0, wasmRefNullType(kWasmAnyRef), wasmRefType(struct)),
      kExprDrop,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprReturn,
  ]);
  // While the value stack type is (ref struct), the source type in the
  // br_on_cast_fail is specified as (ref any), so the branch type becomes
  // the less specific (ref any).
  assertThrows(() => builder.instantiate(),
    WebAssembly.CompileError,
    /type error in branch\[0\] \(expected \(ref 0\), got anyref\)/);
})();

// Test that validation and code generation works fine in unreachable paths.
(function TestBrOnCastUnreachable() {
  print(arguments.callee.name);

  let casts = [
    wasmBrOnCastFail(
      0, wasmRefNullType(kWasmAnyRef), wasmRefNullType(kWasmI31Ref)),
    wasmBrOnCast(0, wasmRefNullType(kWasmAnyRef), wasmRefNullType(kWasmI31Ref)),
    wasmBrOnCastFail(0, wasmRefNullType(kWasmAnyRef), wasmRefType(kWasmI31Ref)),
    wasmBrOnCast(0, wasmRefNullType(kWasmAnyRef), wasmRefType(kWasmI31Ref)),
  ];

  for (let brOnCast of casts) {
    let builder = new WasmModuleBuilder();

    builder.addFunction(`brOnCastUnreachable`,
                        makeSig([kWasmExternRef], []))
    .addBody([
      kExprBlock, kAnyRefCode,
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kExprUnreachable,
        ...brOnCast,
        kExprReturn,
      kExprEnd,
      kExprDrop,
    ]).exportFunc();

    let wasm = builder.instantiate().exports;
    assertTraps(kTrapUnreachable, () => wasm.brOnCastUnreachable());
  }
})();
