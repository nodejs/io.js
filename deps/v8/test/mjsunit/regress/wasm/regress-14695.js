// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-type-reflection --no-wasm-to-js-generic-wrapper

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function Regress14695() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let fun = new WebAssembly.Function({parameters:[], results:[]}, () => {});
  let table = new WebAssembly.Table({element: "anyfunc", initial: 1});
  builder.addImportedTable("m", "table", 1);
  builder.addType(kSig_v_v);
  table.set(0, fun);
  let instance = builder.instantiate({ m: { table: table }});
})();
