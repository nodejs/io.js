// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

// One global that's perfectly OK.
builder.addGlobal(kWasmI32, true, false, [kExprI32Const, 0]);
// One global that's invalid.
builder.addGlobal(kWasmI32, true, false, [kExprUnreachable]);

// The error is the second-to-last byte.
let module_bytes = builder.toBuffer();
assertEquals(kExprUnreachable, module_bytes[18]);
assertEquals(kExprEnd, module_bytes[19]);
assertEquals(20, module_bytes.byteLength);
assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
             "WebAssembly.Module(): opcode unreachable is not allowed in " +
             "constant expressions @+18");
