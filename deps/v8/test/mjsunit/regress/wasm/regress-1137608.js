// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function Regress1137608() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig0 = builder.addType(kSig_i_iii);
  let sig1 = builder.addType(makeSig([kWasmF64, kWasmF64, kWasmI32,
        kWasmI32, kWasmI32, kWasmF32, kWasmI32, kWasmF64, kWasmI32, kWasmF32,
        kWasmI32, kWasmF32, kWasmI32, kWasmF64, kWasmI32], [kWasmI32]));
  let main = builder.addFunction("main", sig0)
      .addBody([
        kExprI64Const, 0,
        kExprF64UConvertI64,
        kExprF64Const, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00,
        kExprF64Const, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
        kExprF64Mul,
        kExprI32Const, 0,
        kExprF64Const, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        kExprF64StoreMem, 0x00, 0xb0, 0xe0, 0xc0, 0x81, 0x03,
        kExprI32Const, 0,
        kExprI32Const, 0,
        kExprI32Const, 0,
        kExprF32Const, 0x00, 0x00, 0x00, 0x00,
        kExprI32Const, 0,
        kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        kExprI32Const, 0,
        kExprF32Const, 0x00, 0x00, 0x00, 0x00,
        kExprI32Const, 0,
        kExprF32Const, 0x00, 0x00, 0x00, 0x00,
        kExprI32Const, 0,
        kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        kExprI32Const, 0,
        kExprI32Const, 2,
        kExprReturnCallIndirect, sig1, kTableZero]).exportFunc();
  builder.addFunction("f", sig1).addBody([kExprI32Const, 0]);
  builder.addTable(kWasmAnyFunc, 4, 4);
  builder.addMemory(16, 32, true);
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module);
})();
