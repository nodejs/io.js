// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

builder.addGlobal(
    kWasmI32, true, false,
    [...wasmI32Const(0x7FFF_FFFF), ...wasmI32Const(100), kExprI32Add]);

builder.addGlobal(
    kWasmI32, true, false,
    [...wasmI32Const(-0x8000_0000), ...wasmI32Const(100), kExprI32Sub]);

builder.addGlobal(
    kWasmI32, true, false,
    [...wasmI32Const(0xFFFF), ...wasmI32Const(0xFFFF), kExprI32Mul]);

builder.addGlobal(
    kWasmI64, true, false,
    [...wasmI64Const(0x7FFF_FFFF_FFFF_FFFFn), ...wasmI64Const(100),
     kExprI64Add]);

builder.addGlobal(
    kWasmI64, true, false,
    [...wasmI64Const(-0x8000_0000_0000_0000n), ...wasmI64Const(100),
     kExprI64Sub]);

builder.addGlobal(
    kWasmI64, true, false,
    [...wasmI64Const(0xFFFF_FFFF), ...wasmI64Const(0xFFFF_FFFF), kExprI64Mul]);

builder.instantiate();
