// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-wrapper-tiering-budget=1

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let instance = (() => {
  let builder = new WasmModuleBuilder();
  builder.startRecGroup();
  let array_type = builder.addArray(kWasmI32, true);
  builder.endRecGroup();
  builder.startRecGroup();

  let sig_index = builder.addType(makeSig([wasmRefType(array_type)], []));
  let imported = builder.addImport('m', 'i', sig_index);
  // To make it eligible for call ref:
  builder.addExport('reexported', imported);

  builder.addFunction(
      "main", makeSig([], []))
    .exportFunc()
    .addBody([
      kExprI32Const, 3,
      kGCPrefix, kExprArrayNewDefault, array_type,
      kExprRefFunc, imported,
      kExprCallRef, sig_index,
    ]);

  return builder.instantiate({m: {i: () => {}}});
})();

instance.exports.main();
