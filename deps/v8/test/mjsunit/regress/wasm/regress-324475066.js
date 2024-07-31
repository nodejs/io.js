// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-stringref --experimental-wasm-imported-strings

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let kRefExtern = wasmRefType(kWasmExternRef);
let kSig_e_i = makeSig([kWasmI32], [kRefExtern]);

const builder = new WasmModuleBuilder();
let kFromCodePoint =
  builder.addImport('wasm:js-string', 'fromCodePoint', kSig_e_i);

builder.addFunction("stringref", kSig_i_v).exportFunc().addBody([
  kExprI32Const, 0x59,
  ...GCInstr(kExprStringFromCodePoint),
  kExprI32Const, 42,
  kExprReturn,
]);
builder.addFunction("importedstring", kSig_i_v).exportFunc().addBody([
  kExprI32Const, 0x59,
  kExprCallFunction, kFromCodePoint,
  kExprI32Const, 42,
  kExprReturn,
]);

const instance = builder.instantiate({}, {builtins: ["js-string"]});
assertThrows(() => instance.exports.stringref(), WebAssembly.RuntimeError,
             "Invalid code point 4294967257");
assertThrows(() => instance.exports.importedstring(), WebAssembly.RuntimeError,
             "Invalid code point 4294967257");
