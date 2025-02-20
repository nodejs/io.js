// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

builder.startRecGroup();
builder.addArray(wasmRefNullType(0), false, kNoSuperType, false);
builder.addArray(wasmRefNullType(2), false, 0);
builder.addArray(kWasmI32, true, 13);
builder.endRecGroup();

assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
             /type 2: invalid supertype 13/);
