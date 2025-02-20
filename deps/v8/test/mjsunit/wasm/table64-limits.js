// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const max_size = 10000000
const oob_size = max_size + 1;
const oob_size_b = BigInt(oob_size);

(function TestTable64OOBInitialSize() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addTable64(kWasmAnyFunc, oob_size).exportAs('table').index;

  const err_msg = `WebAssembly.Module(): initial table size \
(${oob_size} elements) is larger than implementation limit \
(10000000 elements) @+13`;
  assertThrows(
      () => builder.instantiate().exports, WebAssembly.CompileError, err_msg);
})();

(function TestTable64OOBMaxSize() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  // We should not throw an error if the declared maximum size is oob.
  builder.addTable64(kWasmAnyFunc, 1, oob_size).exportAs('table').index;
  builder.instantiate().exports;
})();

(function TestJSTable64OOBInitialSize() {
  print(arguments.callee.name);
  assertThrows(
      () => new WebAssembly.Table(
          {element: 'anyfunc', initial: oob_size_b, address: 'i64'}),
      RangeError, /above the upper bound/);
})();

(function TestJSTable64OOBMaxSize() {
  print(arguments.callee.name);
  new WebAssembly.Table(
      {element: 'anyfunc', initial: 1n, maximum: oob_size_b, address: 'i64'});
})();

(function TestJSTable64OOBGrowSize() {
  print(arguments.callee.name);
  let table = new WebAssembly.Table({
    initial: 1n,
    maximum: oob_size_b + 10n,
    element: 'anyfunc',
    address: 'i64'
  });
  assertThrows(
      () => table.grow(oob_size_b - 1n), RangeError, /failed to grow table/);
})();

function exportTable64Grow(builder, table) {
  let kSig_l_rl = makeSig([kWasmExternRef, kWasmI64], [kWasmI64]);
    builder.addFunction('table64_grow', kSig_l_rl)
        .addBody([
          kExprLocalGet, 0,
          kExprLocalGet, 1,
          kNumericPrefix, kExprTableGrow, table.index])
        .exportFunc();
}

(function TestTable64OOBGrowSize() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const initial_size = 1;
  const table = builder.addTable64(kWasmExternRef, initial_size, oob_size + 10)
                    .exportAs('table');

  exportTable64Grow(builder, table);
  let exports = builder.instantiate().exports;

  assertEquals(-1n, exports.table64_grow('64 bit', oob_size_b - 1n));
})();

(function TestJSTable64MaxUint64() {
  print(arguments.callee.name);
  let max_uint64 = (1n << 64n) - 1n;
  let table = new WebAssembly.Table(
      {initial: 0n, maximum: max_uint64, element: 'anyfunc', address: 'i64'});
  table.grow(oob_size_b - 1n);
  assertThrows(() => table.grow(1n), RangeError, /failed to grow table/);
})();

(function TestJSTable64MaxUint64PlusOne() {
  print(arguments.callee.name);
  let max_uint64 = (1n << 64n) - 1n;
  assertThrows(
      () => new WebAssembly.Table({
        initial: 1n,
        maximum: max_uint64 + 1n,
        element: 'anyfunc',
        address: 'i64'
      }),
      TypeError,
      'WebAssembly.Table(): Property \'maximum\' must be in u64 range');
})();
