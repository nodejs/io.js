// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

let table = builder.addTable(kWasmFuncRef, 10);

builder.addActiveElementSegment(table.index, [kExprI32Const, 0], [],
                                wasmRefNullType(0));

assertThrows(() => builder.instantiate(), WebAssembly.CompileError);
