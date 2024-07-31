// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder.addMemory(16, 32);

let type8 = builder.addType({params: [], results: []});
let gc_module_marker = builder.addStruct([]);

let func1 = builder.addFunction("func1", type8).exportFunc().addBody([]);

let main = builder.addFunction("main", kSig_i_iii).exportFunc();
main.addBody([
  kExprRefFunc, func1.index,
  kExprCallRef, type8,
  // if (x == 0) return 0;
  kExprI32Const, 0,
  kExprLocalGet, 0,
  kExprI32Eqz,
  kExprBrIf, 0,
  kExprDrop,
  // return main(x - 1);
  kExprLocalGet, 0,
  kExprI32Const, 1,
  kExprI32Sub,
  kExprI32Const, 0,
  kExprMemorySize, 0,
  kExprReturnCall, main.index,
]);

let instance = builder.instantiate();
let f = instance.exports.main;

f(100);
%WasmTierUpFunction(f);
f();
