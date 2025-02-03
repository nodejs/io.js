// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const builder = new WasmModuleBuilder();
let table = new WebAssembly.Table({element: 'anyfunc', initial: 2});
// Big size that causes an int32 overflow.
builder.addImportedTable('m', 'table', 4000000000);
assertThrows(
    () => builder.instantiate({m: {table: table}}), WebAssembly.CompileError);
