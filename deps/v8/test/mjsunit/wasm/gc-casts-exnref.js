// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-exnref --no-experimental-wasm-inlining

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let getExnRef = function() {
  let tag = new WebAssembly.Tag({parameters: []});
  return new WebAssembly.Exception(tag, []);
}

// Helper module to produce an exnref or convert a JS value to an exnref.
let helper = (function () {
  let builder = new WasmModuleBuilder();
  let tag_index = builder.addTag(kSig_v_v);
  let throw_index = builder.addImport('m', 'import', kSig_v_r);
  builder.addFunction('get_exnref', makeSig([], [kWasmExnRef]))
      .addBody([
          kExprTryTable, kWasmVoid, 1,
          kCatchAllRef, 0,
          kExprThrow, tag_index,
          kExprEnd,
          kExprUnreachable,
      ]).exportFunc();
  builder.addFunction('to_exnref', makeSig([kWasmExternRef], [kWasmExnRef]))
      .addBody([
          kExprTryTable, kWasmVoid, 1,
          kCatchAllRef, 0,
          kExprLocalGet, 0,
          kExprCallFunction, throw_index,
          kExprEnd,
          kExprUnreachable,
      ]).exportFunc();
  function throw_js(r) { throw r; }
  let instance = builder.instantiate({m: {import: throw_js}});
  return instance;
})();

(function RefTestExnRef() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let get_exnref = builder.addImport('m', 'get_exnref', makeSig([], [kWasmExnRef]));
  builder.addFunction('testExnRef',
      makeSig([], [kWasmI32, kWasmI32, kWasmI32, kWasmI32]))
    .addLocals(kWasmExnRef, 1)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprRefTest, kExnRefCode,
      kExprLocalGet, 0,
      kGCPrefix, kExprRefTest, kNullExnRefCode,
      kExprCallFunction, get_exnref,
      kGCPrefix, kExprRefTest, kExnRefCode,
      kExprCallFunction, get_exnref,
      kGCPrefix, kExprRefTest, kNullExnRefCode,
    ]).exportFunc();

  builder.addFunction('testNullExnRef',
      makeSig([], [kWasmI32, kWasmI32, kWasmI32, kWasmI32]))
    .addLocals(kWasmExnRef, 1)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprRefTestNull, kExnRefCode,
      kExprLocalGet, 0,
      kGCPrefix, kExprRefTestNull, kNullExnRefCode,
      kExprCallFunction, get_exnref,
      kGCPrefix, kExprRefTestNull, kExnRefCode,
      kExprCallFunction, get_exnref,
      kGCPrefix, kExprRefTestNull, kNullExnRefCode,
    ]).exportFunc();


  let instance = builder.instantiate({m: {get_exnref: helper.exports.get_exnref}});
  let wasm = instance.exports;
  assertEquals([0, 0, 1, 0], wasm.testExnRef());
  assertEquals([1, 1, 1, 0], wasm.testNullExnRef());
})();

(function RefCastExnRef() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let to_exnref = builder.addImport('m', 'to_exnref', makeSig([kWasmExternRef], [kWasmExnRef]));

  builder.addFunction('castToExnRef',
      makeSig([kWasmExternRef], []))
    .addBody([
        kExprLocalGet, 0,
        kExprCallFunction, to_exnref,
        kGCPrefix, kExprRefCast, kExnRefCode,
        kExprThrowRef])
    .exportFunc();
  builder.addFunction('castToNullExnRef',
    makeSig([kWasmExternRef], []))
  .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, to_exnref,
      kGCPrefix, kExprRefCast, kNullExnRefCode,
      kExprDrop])
  .exportFunc();
  builder.addFunction('castNullToExnRef',
    makeSig([kWasmExternRef], []))
  .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, to_exnref,
      kGCPrefix, kExprRefCastNull, kExnRefCode,
      kExprThrowRef])
  .exportFunc();
  builder.addFunction('castNullToNullExnRef',
    makeSig([kWasmExternRef], []))
  .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, to_exnref,
      kGCPrefix, kExprRefCastNull, kNullExnRefCode,
      kGCPrefix, kExprRefCastNull, kExnRefCode,
      kExprThrowRef])
  .exportFunc();

  let instance = builder.instantiate({m: {to_exnref: helper.exports.to_exnref}});
  let wasm = instance.exports;

  let exnRef = getExnRef();
  assertTraps(kTrapIllegalCast, () => wasm.castToExnRef(null));
  assertThrowsEquals(() => wasm.castToExnRef(exnRef), exnRef);
  assertTraps(kTrapIllegalCast, () => wasm.castToNullExnRef(null));
  assertTraps(kTrapIllegalCast, () => wasm.castToNullExnRef(exnRef));

  assertThrows(() => wasm.castNullToExnRef(null), Error, /rethrowing null value/);
  assertThrowsEquals(() => wasm.castNullToExnRef(exnRef), exnRef);
  assertThrows(() => wasm.castNullToNullExnRef(null), Error, /rethrowing null value/);
  assertTraps(kTrapIllegalCast, () => wasm.castNullToNullExnRef(exnRef));
})();

(function BrOnCastExnRef() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let to_exnref = builder.addImport('m', 'to_exnref', makeSig([kWasmExternRef], [kWasmExnRef]));

  builder.addFunction('castToExnRef',
    makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprBlock, kWasmRef, kExnRefCode,
      kExprLocalGet, 0,
      kExprCallFunction, to_exnref,
      ...wasmBrOnCast(
          0, wasmRefNullType(kWasmExnRef), wasmRefType(kWasmExnRef)),
      kExprI32Const, 0,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 1,
    kExprReturn,
  ])
  .exportFunc();
  builder.addFunction('castToNullExnRef',
    makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprBlock, kWasmRef, kNullExnRefCode,
      kExprLocalGet, 0,
      kExprCallFunction, to_exnref,
      ...wasmBrOnCast(
          0, wasmRefNullType(kWasmExnRef), wasmRefType(kWasmNullExnRef)),
      kExprI32Const, 0,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 1,
    kExprReturn,
  ])
  .exportFunc();

  builder.addFunction('castNullToExnRef',
    makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprBlock, kWasmRefNull, kExnRefCode,
      kExprLocalGet, 0,
      kExprCallFunction, to_exnref,
      ...wasmBrOnCast(0,
          wasmRefNullType(kWasmExnRef), wasmRefNullType(kWasmExnRef)),
      kExprI32Const, 0,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 1,
    kExprReturn,
  ])
  .exportFunc();
  builder.addFunction('castNullToNullExnRef',
    makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprBlock, kWasmRefNull, kNullExnRefCode,
      kExprLocalGet, 0,
      kExprCallFunction, to_exnref,
      ...wasmBrOnCast(0,
          wasmRefNullType(kWasmExnRef), wasmRefNullType(kWasmNullExnRef)),
      kExprI32Const, 0,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 1,
    kExprReturn,
  ])
  .exportFunc();

  builder.addFunction('castFailToExnRef',
    makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprBlock, kWasmRefNull, kExnRefCode,
      kExprLocalGet, 0,
      kExprCallFunction, to_exnref,
      ...wasmBrOnCastFail(0,
          wasmRefNullType(kWasmExnRef), wasmRefType(kWasmExnRef)),
      kExprI32Const, 0,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 1,
    kExprReturn,
  ])
  .exportFunc();
  builder.addFunction('castFailToNullExnRef',
    makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprBlock, kWasmRefNull, kExnRefCode,
      kExprLocalGet, 0,
      kExprCallFunction, to_exnref,
      ...wasmBrOnCastFail(0,
          wasmRefNullType(kWasmExnRef), wasmRefType(kWasmNullExnRef)),
      kExprI32Const, 0,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 1,
    kExprReturn,
  ])
  .exportFunc();

  builder.addFunction('castFailNullToExnRef',
    makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprBlock, kWasmRef, kExnRefCode,
      kExprLocalGet, 0,
      kExprCallFunction, to_exnref,
      ...wasmBrOnCastFail(0,
          wasmRefNullType(kWasmExnRef), wasmRefNullType(kWasmExnRef)),
      kExprI32Const, 0,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 1,
    kExprReturn,
  ])
  .exportFunc();
  builder.addFunction('castFailNullToNullExnRef',
    makeSig([kWasmExternRef], [kWasmI32]))
  .addBody([
    kExprBlock, kWasmRef, kExnRefCode,
      kExprLocalGet, 0,
      kExprCallFunction, to_exnref,
      ...wasmBrOnCastFail(0,
          wasmRefNullType(kWasmExnRef), wasmRefNullType(kWasmNullExnRef)),
      kExprI32Const, 0,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 1,
    kExprReturn,
  ])
  .exportFunc();

  let instance = builder.instantiate({m: {to_exnref: helper.exports.to_exnref}});
  let wasm = instance.exports;
  let exnRef = getExnRef();

  assertEquals(0, wasm.castToExnRef(null));
  assertEquals(1, wasm.castToExnRef(exnRef));

  assertEquals(0, wasm.castToNullExnRef(null));
  assertEquals(0, wasm.castToNullExnRef(exnRef));

  assertEquals(1, wasm.castNullToExnRef(null));
  assertEquals(1, wasm.castNullToExnRef(exnRef));

  assertEquals(1, wasm.castNullToNullExnRef(null));
  assertEquals(0, wasm.castNullToNullExnRef(exnRef));

  assertEquals(1, wasm.castFailToExnRef(null));
  assertEquals(0, wasm.castFailToExnRef(exnRef));

  assertEquals(1, wasm.castFailToNullExnRef(null));
  assertEquals(1, wasm.castFailToNullExnRef(exnRef));

  assertEquals(0, wasm.castFailNullToExnRef(null));
  assertEquals(0, wasm.castFailNullToExnRef(exnRef));

  assertEquals(0, wasm.castFailNullToNullExnRef(null));
  assertEquals(1, wasm.castFailNullToNullExnRef(exnRef));
})();
