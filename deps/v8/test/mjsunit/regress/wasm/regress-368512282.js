// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let exporting_instance = (function () {
  let b = new WasmModuleBuilder();
  // So that the table's type has larger index than the importing instance's
  // type index space.
  b.addType(kSig_i_i);
  let table_type = b.addType(kSig_i_ii);
  b.addTable(wasmRefNullType(table_type), 10, 10, undefined, false, false)
   .exportAs("exported_table");
  return b.instantiate();
})();

let b = new WasmModuleBuilder();
let table_type = b.addType(kSig_i_ii);
let table = b.addImportedTable("m", "t", 10, 10, wasmRefNullType(table_type));

b.instantiate({m: {t: exporting_instance.exports.exported_table}});
