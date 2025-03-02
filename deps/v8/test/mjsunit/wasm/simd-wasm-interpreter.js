// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// Tests that R2R/S2R mode is not supported for Simd types in the Wasm
// interpreter.
(function testSimdSelect() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  builder.addType(makeSig([], []));
  builder.addFunction('main', 0 /* sig */)
    .addLocals(kWasmS128, 2)
    .exportFunc()
    .addBody([
      ...wasmS128Const(new Array(16).fill(0)),  // s128.const
      ...wasmS128Const(new Array(16).fill(0)),  // s128.const
      kExprI32Const, 0,
      kExprSelect,                              // select
      kExprDrop,
    ]);
  var instance = builder.instantiate();
  instance.exports.main();
})();

// Tests R2S mode Select instruction for Simd types
(function testSimdSelectR2S() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  let globalint = builder.addImportedGlobal('o', 'g', kWasmI32, true);

  builder.addType(makeSig([], []));
  builder.addFunction('main', 0 /* sig */)
    .addLocals(kWasmS128, 2)
    .exportFunc()
    .addBody([
      ...wasmS128Const(new Array(16).fill(1)),  // s128.const
      ...wasmS128Const(new Array(16).fill(0)),  // s128.const
      kExprGlobalGet, globalint,
      kExprSelect,                              // select

      // The first s128 const should be selected.
      ...wasmS128Const(new Array(16).fill(1)),
      kSimdPrefix, kExprI8x16Ne,
      kSimdPrefix, kExprI8x16AllTrue,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,
    ]);
  let gint = new WebAssembly.Global({mutable: true, value: 'i32'});
  gint.value = 1;
  var instance = builder.instantiate({o: {g: gint}});
  instance.exports.main();
})();

(function TestThreadInWasmAfterCatchingSignatureMismatch() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  builder.addMemory(1, 32);
  var kSig_s_v = makeSig([], [kWasmS128]);
  let sig_index = builder.addType(kSig_i_i);
  let sig_index_l_v = builder.addType(kSig_l_v);

  let table2 = new WebAssembly.Table({
    initial: 10, maximum: 10, element: 'anyfunc',
  });
  builder.addImportedTable('o', 'table');

  let table = builder.addTable(kWasmFuncRef, 2, 19);

  builder.addImport("o", "fn", kSig_l_v);
  builder.addExport("fn", 0);

  const func0 = builder.addFunction("test_ref_call", sig_index);

  const func1_function = builder.addFunction("func1", kSig_s_v)
    .addBody([
      ...wasmI32Const(0),
      kSimdPrefix, kExprI8x16Splat
    ]);

  const func0_function = func0.addBody([
    kExprTry, kWasmVoid,
      kExprRefFunc, func1_function.index,
      kExprCallRef, 3,
      kExprDrop,
    kExprCatchAll,
    kExprEnd,
    kExprLocalGet, 0,
    kExprI32LoadMem, 0, 0,
  ]).exportAs("test_ref_call");

  builder.addActiveElementSegment(
    table.index, wasmI32Const(0),
    [[kExprRefFunc, func0_function.index],
    [kExprRefFunc, func1_function.index]],
    table.type);

  builder.addFunction("test_indirect_call", sig_index).addBody([
    kExprTry, kWasmVoid,
      kExprI32Const, 0,
      kExprCallIndirect, sig_index_l_v, kTableZero,
      kExprDrop,
    kExprCatchAll,
    kExprEnd,
    kExprLocalGet, 0,
    kExprI32LoadMem, 0, 0,
  ]).exportAs("test_indirect_call");

  builder.addFunction("test_import_call", sig_index).addBody([
    kExprTry, kWasmVoid,
      kExprCallFunction, 0,
      kExprDrop,
    kExprCatchAll,
    kExprEnd,
    kExprLocalGet, 0,
    kExprI32LoadMem, 0, 0,
  ]).exportAs("test_import_call");

  let instance = builder.instantiate({
    o: {
      table: table2,
      fn: () => {
        return 3.14; // Cause an type error casting to I64.
      },
    }
  });
  table2.set(0, instance.exports.fn);

  // Assert that the OOB memory access is correctly handled by the trap handler.

  // Ref call.
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.test_ref_call(0x44444444));

  // Indirect call.
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.test_indirect_call(0x44444444));

  // Import call.
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.test_import_call(0x44444444));
})();

(function TestSIMDStoreLanePartialOOB() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);

  builder.addFunction("main", kSig_v_v).addBody([
    ...wasmI32Const(0),
    ...wasmI32Const(0),
    kSimdPrefix, kExprI8x16Splat,
    kSimdPrefix, kExprS128Store8Lane, 0, ...wasmUnsignedLeb(0xffff), 0,
    ...wasmI32Const(0),
    ...wasmI32Const(0),
    kSimdPrefix, kExprI8x16Splat,
    kSimdPrefix, kExprS128Load8Lane, 0, ...wasmUnsignedLeb(0xffff), 0,
    kExprDrop
  ]).exportAs("main");

  let instance = builder.instantiate();
  instance.exports.main();
})();

(function testMem64Simd() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  builder.addMemory64(1, 1, false);
  let INDEX = 0;
  let OFFSET = 0x40;

  builder.addFunction("main", kSig_v_v)
    .addBody([
      ...wasmI64Const(INDEX),
      ...wasmI64Const(0x0fedcba978654321n),
      kExprI64StoreMem, 0, OFFSET,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load8Splat, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 1,
      ...wasmI64Const(0x2121212121212121n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
          kExprUnreachable,
      kExprEnd,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load16Splat, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 1,
      ...wasmI64Const(0x4321432143214321n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load32Splat, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 1,
      ...wasmI64Const(0x7865432178654321n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load64Splat, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 1,
      ...wasmI64Const(0x0fedcba978654321n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load8x8U, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 1,
      ...wasmI64Const(0x000f00ed00cb00a9n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load8x8S, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 1,
      ...wasmI64Const(0x000fffedffcbffa9n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load16x4U, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 1,
      ...wasmI64Const(0x00000fed0000cba9n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load16x4S, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 1,
      ...wasmI64Const(0x00000fedffffcba9n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load32x2U, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 1,
      ...wasmI64Const(0x0000000000fedcba9n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load32x2S, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 0,
      ...wasmI64Const(0x00000000078654321n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load32Zero, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 0,
      ...wasmI64Const(0x00000000078654321n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,

      ...wasmI64Const(INDEX),
      kSimdPrefix, kExprS128Load64Zero, 0, OFFSET,
      kSimdPrefix, kExprI64x2ExtractLane, 0,
      ...wasmI64Const(0x0fedcba978654321n),
      kExprI64Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd
    ])
    .exportAs("main");

  let instance = builder.instantiate();
  instance.exports.main();
})();
