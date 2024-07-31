// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

builder.addType(kSig_v_v);
builder.addType(kSig_v_i);
builder.addType(kSig_i_v);

builder.addGlobal(wasmRefNullType(3), true, false, [kExprRefNull, 3]);

assertThrows(() => builder.instantiate(), WebAssembly.CompileError);
