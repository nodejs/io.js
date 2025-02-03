// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function exportTable64Getter(builder, table, type) {
  const table64_get_sig = makeSig([kWasmI64], [type]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody([
        kExprLocalGet, 0,
        kExprTableGet, table.index])
      .exportFunc();
}

function exportTable64Size(builder, table) {
  builder.addFunction('table64_size', kSig_l_v)
      .addBody([kNumericPrefix, kExprTableSize, table.index])
      .exportFunc();
}

function exportTable64Init(builder, table, passive) {
  builder.addFunction('table64_init', kSig_v_lii)
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kExprLocalGet, 2,
        kNumericPrefix, kExprTableInit, passive, table.index
      ])
      .exportFunc();
}

function addPassiveSegmentWithExportedFuncs(builder) {
  let sig = builder.addType(kSig_i_v, kNoSuperType, false);
  let f1 =
      builder.addFunction('f1', sig).addBody([kExprI32Const, 11]).exportFunc();
  let f2 =
      builder.addFunction('f2', sig).addBody([kExprI32Const, 22]).exportFunc();
  let f3 =
      builder.addFunction('f3', sig).addBody([kExprI32Const, 33]).exportFunc();
  let passive = builder.addPassiveElementSegment(
      [
        [kExprRefFunc, f1.index], [kExprRefFunc, f2.index],
        [kExprRefFunc, f3.index]
      ],
      wasmRefType(0));
  return passive;
}

(function TestTable64Init() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  let passive = addPassiveSegmentWithExportedFuncs(builder);
  const table = builder.addTable64(kWasmAnyFunc, 5, 5).exportAs('table');

  exportTable64Init(builder, table, passive);
  exportTable64Getter(builder, table, kWasmAnyFunc);
  exportTable64Size(builder, table);

  let exports = builder.instantiate().exports;

  const dst = 1n;
  const src = 0;
  const size = 3;
  exports.table64_init(dst, src, size);

  assertEquals(5n, exports.table64_size());
  assertEquals(null, exports.table64_get(0n));
  assertSame(exports.f1, exports.table64_get(dst));
  assertEquals(11, exports.table64_get(dst)());
  assertSame(exports.f2, exports.table64_get(dst + 1n));
  assertEquals(22, exports.table64_get(dst + 1n)());
  assertSame(exports.f3, exports.table64_get(dst + 2n));
  assertEquals(33, exports.table64_get(dst + 2n)());
  assertEquals(null, exports.table64_get(4n));
})();

(function TestTable64InitOOB() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  let passive = addPassiveSegmentWithExportedFuncs(builder);
  const table = builder.addTable64(kWasmAnyFunc, 5, 5).exportAs('table');

  exportTable64Init(builder, table, passive);
  exportTable64Getter(builder, table, kWasmAnyFunc);
  exportTable64Size(builder, table);

  let exports = builder.instantiate().exports;

  const src = 0;
  const size = 3;
  // Just in bouds.
  let dst = 2n;
  exports.table64_init(dst, src, size);
  assertEquals(5n, exports.table64_size());
  assertSame(null, exports.table64_get(0n));
  assertSame(null, exports.table64_get(1n));
  assertSame(exports.f1, exports.table64_get(2n));
  assertSame(exports.f2, exports.table64_get(3n));
  assertSame(exports.f3, exports.table64_get(4n));
  // OOB.
  dst = 3n;
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_init(dst, src, size));
  dst = 1n << 32n;
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_init(dst, src, size));
})();
