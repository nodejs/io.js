// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let tag0 = builder.addTag(kSig_v_v);

builder.startRecGroup();
let kArrayI8 = builder.addArray(kWasmI8, true, kNoSuperType, true);
builder.endRecGroup();
let kArray8Ref = wasmRefNullType(kArrayI8);
let kRefExtern = wasmRefType(kWasmExternRef);
let kSig_e_i = makeSig([kWasmI32], [kRefExtern]);
let kStringFromCharCode =
    builder.addImport('wasm:js-string', 'fromCharCode', kSig_e_i);
let kStringFromUtf8Array = builder.addImport(
    'wasm:text-decoder', 'decodeStringFromUTF8Array',
    makeSig([kArray8Ref, kWasmI32, kWasmI32], [kRefExtern]));

let main = builder.addFunction('main', kSig_e_i).exportFunc().addBody([
  kExprTry, kWasmVoid,
    kExprI32Const, 97,
    // The FunctionBodyDecoder assumes that any call can throw, so it marks
    // the {catch_all} block as reachable.
    // The graph builder interface recognizes the well-known import and knows
    // that it won't throw, so it considers the {catch_all} block (and
    // everything after it) unreachable.
    kExprCallFunction, kStringFromCharCode,
    kExprReturn,
  kExprCatch, tag0,
  kExprCatchAll,
  kExprEnd,
  kExprRefNull, kArrayI8,
  kExprI32Const, 0,
  kExprI32Const, 0,
  // The point of this test: what happens for this instruction when the
  // compiler knows it's emitting unreachable code.
  kExprCallFunction, kStringFromUtf8Array,
]);

builder.addFunction('inlined', kSig_e_i).exportFunc().addBody([
  kExprLocalGet, 0,
  kExprCallFunction, main.index,
]);

let callee_no_catch = builder.addFunction('', kSig_e_i).addBody([
  kExprI32Const, 98,
  kExprCallFunction, kStringFromCharCode,
  kExprReturn,
]);

builder.addFunction('inlined_catch', kSig_e_i).exportFunc().addBody([
  kExprTry, kWasmVoid,
    kExprLocalGet, 0,
    kExprCallFunction, callee_no_catch.index,
    kExprReturn,
  kExprCatchAll,
  kExprEnd,
  kExprRefNull, kArrayI8,
  kExprI32Const, 0,
  kExprI32Const, 0,
  kExprCallFunction, kStringFromUtf8Array,
]);

let kBuiltins = {builtins: ['js-string', 'text-decoder']};
let instance = builder.instantiate({}, kBuiltins);
instance.exports.main(1);
instance.exports.inlined(1);
instance.exports.inlined_catch(1);
