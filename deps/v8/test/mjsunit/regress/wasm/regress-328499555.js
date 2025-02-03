// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let createI31Ref = [...wasmI32Const(836029236), kGCPrefix, kExprRefI31];

const builder = new WasmModuleBuilder();
builder.addStruct([], kNoSuperType, false);
builder.addArray(kWasmI32, true, kNoSuperType, false);
builder.addType(makeSig([], [kWasmI32]));
builder.addMemory(16, 32);
builder.addGlobal(wasmRefType(kWasmI31Ref), 0, 0, createI31Ref);
builder.addFunction('main', makeSig([], [kWasmI32]))
.addBody([
  ...createI31Ref,
  kExprGlobalGet, 0,
  kExprRefEq,
]).exportFunc();

let kBuiltins = { builtins: ['js-string', 'text-decoder', 'text-encoder'] };
const instance = builder.instantiate({}, kBuiltins);
assertEquals(1, instance.exports.main());
