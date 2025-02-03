// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-max-table-size=10

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// With the flags we set the maximum table size to 10, so 11 is out-of-bounds.
const oob = 11;

(function TestJSTableInitialAboveTheLimit() {
  print(arguments.callee.name);
  assertThrows(
    () => new WebAssembly.Table({ initial: oob, element: "anyfunc" }),
    RangeError, /above the upper bound/);
})();

(function TestJSTableMaximumAboveTheLimit() {
  print(arguments.callee.name);
  let table =
      new WebAssembly.Table({initial: 1, maximum: oob, element: 'anyfunc'});
  assertThrows(() => table.grow(oob - 1), RangeError, /failed to grow table/);
})();

(function TestDecodeTableInitialAboveTheLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.setTableBounds(oob);

  const err_msg = `WebAssembly.Module(): initial table size \
(11 elements) is larger than implementation limit (10 elements) @+13`;
  assertThrows(
    () => builder.instantiate(),
    WebAssembly.CompileError, err_msg);
})();

(function TestDecodeTableMaximumAboveTheLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.setTableBounds(1, oob);
  // Should not throw, as the table does not exceed the limit at instantiation
  // time.
  builder.instantiate();
})();
