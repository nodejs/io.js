// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestTable64Size() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table_index =
      builder.addTable64(kWasmAnyFunc, 15, 30).exportAs('table').index;
  builder.addFunction('table64_size', kSig_l_v)
      .addBody([kNumericPrefix, kExprTableSize, table_index])
      .exportFunc();

  let exports = builder.instantiate().exports;

  assertEquals(15n, exports.table64_size());
})();
