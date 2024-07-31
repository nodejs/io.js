// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

let struct_type = builder.addStruct([makeField(kWasmI32, true)]);

builder.addFunction('main', kSig_v_v).exportFunc()
  .addBody([
  kExprRefNull, struct_type,
  kExprRefAsNonNull,
  kGCPrefix, kExprStructGet, struct_type, 0,
  kExprDrop,
  kExprI32Const, 1,
  ...wasmF32Const(42),
  kExprF32Const, 0xd7, 0xff, 0xff, 0xff,  // -nan:0x7fffd7
  kExprF32Gt,
  kExprI32DivU,
  kExprIf, kWasmVoid,
  kExprUnreachable,
  kExprEnd,
]);

let main = builder.instantiate().exports.main;
assertThrows(
    () => main(), WebAssembly.RuntimeError, /dereferencing a null pointer/);
