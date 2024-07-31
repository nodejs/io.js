// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-imported-strings
// Flags: --no-wasm-loop-unrolling --no-wasm-loop-peeling

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let externExternSig = builder.addType(
  makeSig([kWasmExternRef], [kWasmExternRef]));
let fromCharCodeSig = builder.addType(
  makeSig([kWasmI32], [wasmRefType(kWasmExternRef)]));
let fromCodePoint = builder.addImport(
  'wasm:js-string', 'fromCodePoint', fromCharCodeSig);

builder.addFunction('main', makeSig([], []))
  .addLocals(kWasmI32, 1) // loop counter
  .addBody([
kExprI32Const, 5,
kExprLocalTee, 0,
kExprCallFunction, fromCodePoint,
kExprLoop, externExternSig,
  kExprDrop,
  kExprRefNull, kExternRefCode,
  kExprLocalGet, 0,
  kExprI32Const, 1,
  kExprI32Sub,
  kExprLocalTee, 0,
  kExprBrIf, 0,
kExprEnd,
kExprDrop,
]).exportFunc();
let kBuiltins = { builtins: ['js-string', 'text-decoder', 'text-encoder'] };
const instance = builder.instantiate({}, kBuiltins);
instance.exports.main();
