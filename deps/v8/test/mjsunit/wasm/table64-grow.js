// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function exportTable64Grow(builder, table) {
let kSig_l_rl = makeSig([kWasmExternRef, kWasmI64], [kWasmI64]);
  builder.addFunction('table64_grow', kSig_l_rl)
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kNumericPrefix, kExprTableGrow, table.index])
      .exportFunc();
}

function exportTable64Size(builder, table) {
    builder.addFunction('table64_size', kSig_l_v)
    .addBody([kNumericPrefix, kExprTableSize, table.index])
    .exportFunc();
}

(function TestTable64GrowExternRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const initial_size = 7;
  const max_size = 20;
  const table = builder.addTable64(kWasmExternRef, initial_size, max_size)
                    .exportAs('table');

  exportTable64Grow(builder, table);
  exportTable64Size(builder, table);
  let exports = builder.instantiate().exports;

  assertEquals(BigInt(initial_size), exports.table64_size());
  assertEquals(BigInt(initial_size), exports.table64_grow('externref', 3n));
  assertEquals(BigInt(initial_size) + 3n, exports.table64_size());
  const oob_size = 11n;
  assertEquals(-1n, exports.table64_grow('too big', oob_size));
  const oob64_size = 1n << 32n;
  assertEquals(-1n, exports.table64_grow('64 bit', oob64_size));
})();
