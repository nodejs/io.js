// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var builder = new WasmModuleBuilder();
builder.addImportedTable("x", "table", 1, 10000000);
let module = new WebAssembly.Module(builder.toBuffer());
let table = new WebAssembly.Table({element: "anyfunc",
  initial: 1, maximum:1000000});
let instance = new WebAssembly.Instance(module, {x: {table:table}});

assertThrows(() => table.grow(Infinity), TypeError);
