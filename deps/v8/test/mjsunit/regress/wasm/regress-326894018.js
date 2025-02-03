// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --no-wasm-lazy-compilation --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.startRecGroup();
builder.addArray(kWasmI8, true, kNoSuperType, true);
builder.endRecGroup();
builder.startRecGroup();
builder.addArray(kWasmI16, true, kNoSuperType, true);
builder.endRecGroup();
builder.addStruct([makeField(kWasmS128, false), makeField(kWasmI8, true), makeField(wasmRefType(0), true)], kNoSuperType, false);
builder.addStruct([makeField(kWasmS128, false), makeField(kWasmI8, true), makeField(wasmRefType(0), true), makeField(kWasmI8, true), makeField(kWasmI8, true)], 2, false);
builder.addArray(kWasmI32, true, kNoSuperType, false);
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.startRecGroup();
builder.addType(makeSig([kWasmEqRef, wasmRefType(kWasmFuncRef), kWasmFuncRef, wasmRefNullType(kWasmArrayRef), kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32], [wasmRefType(1), wasmRefType(kWasmExternRef), wasmRefNullType(3), wasmRefType(kWasmFuncRef), wasmRefType(5), kWasmI32, wasmRefNullType(kWasmNullFuncRef), kWasmI64, kWasmI64, kWasmI64, wasmRefNullType(7), wasmRefType(5), wasmRefType(kWasmFuncRef), wasmRefNullType(kWasmNullFuncRef)]));
builder.addType(makeSig([], []));
builder.endRecGroup();
builder.addType(makeSig([], []));
builder.addType(makeSig([kWasmExternRef], [wasmRefType(kWasmExternRef)]));
builder.addType(makeSig([kWasmExternRef], [kWasmI32]));
builder.addType(makeSig([kWasmI32], [wasmRefType(kWasmExternRef)]));
builder.addType(makeSig([kWasmExternRef, kWasmI32], [kWasmI32]));
builder.addType(makeSig([kWasmExternRef, kWasmExternRef], [wasmRefType(kWasmExternRef)]));
builder.addType(makeSig([kWasmExternRef, kWasmI32, kWasmI32], [wasmRefType(kWasmExternRef)]));
builder.addType(makeSig([kWasmExternRef, kWasmExternRef], [kWasmI32]));
builder.addType(makeSig([wasmRefNullType(1), kWasmI32, kWasmI32], [wasmRefType(kWasmExternRef)]));
builder.addType(makeSig([kWasmExternRef, wasmRefNullType(1), kWasmI32], [kWasmI32]));
builder.addType(makeSig([kWasmExternRef, wasmRefNullType(0), kWasmI32], [kWasmI32]));
builder.addType(makeSig([kWasmExternRef], [wasmRefType(0)]));
builder.addType(makeSig([wasmRefNullType(0), kWasmI32, kWasmI32], [wasmRefType(kWasmExternRef)]));
builder.addImport('wasm:js-string', 'cast', 9 /* sig */);
builder.addImport('wasm:js-string', 'test', 10 /* sig */);
builder.addImport('wasm:js-string', 'fromCharCode', 11 /* sig */);
builder.addImport('wasm:js-string', 'fromCodePoint', 11 /* sig */);
builder.addImport('wasm:js-string', 'charCodeAt', 12 /* sig */);
builder.addImport('wasm:js-string', 'codePointAt', 12 /* sig */);
builder.addImport('wasm:js-string', 'length', 10 /* sig */);
builder.addImport('wasm:js-string', 'concat', 13 /* sig */);
builder.addImport('wasm:js-string', 'substring', 14 /* sig */);
builder.addImport('wasm:js-string', 'equals', 15 /* sig */);
builder.addImport('wasm:js-string', 'compare', 15 /* sig */);
builder.addImport('wasm:js-string', 'fromCharCodeArray', 16 /* sig */);
builder.addImport('wasm:js-string', 'intoCharCodeArray', 17 /* sig */);
builder.addImport('wasm:text-encoder', 'measureStringAsUTF8', 10 /* sig */);
builder.addImport('wasm:text-encoder', 'encodeStringIntoUTF8Array', 18 /* sig */);
builder.addImport('wasm:text-encoder', 'encodeStringToUTF8Array', 19 /* sig */);
builder.addImport('wasm:text-decoder', 'decodeStringFromUTF8Array', 20 /* sig */);
builder.addMemory(16, 32);
builder.addTable(kWasmFuncRef, 4, 4, undefined);
builder.addActiveElementSegment(0, wasmI32Const(0), [[kExprRefFunc, 17]], kWasmFuncRef);
builder.addTag(makeSig([], []));
// Generate function 18 (out of 21).
builder.addFunction(undefined, 5 /* sig */)
  .addLocals(wasmRefType(2), 1).addLocals(kWasmI32, 1).addLocals(wasmRefNullType(0), 29).addLocals(kWasmI32, 2)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprI32Const, 0xc7, 0x00,  // i32.const
kExprTry, 0x40,  // try @20
  kExprLoop, 0x40,  // loop @48
    kExprRefNull, 0x6f,
    kExprRefNull, 0x6f,  // ref.null
    kExprCallFunction, 0x09,  // call function #9: i_nn
    kExprIf, 0x40,  // if @189
      kExprBr, 0x01,  // br depth=1
      kExprEnd,  // end @193
    kExprEnd,  // end @194
kExprCatch, 0x00,  // catch @208
  kExprEnd,  // end @210
kSimdPrefix, kExprS128Load8x8U, 0x03, 0xc7, 0x8f, 0x03,  // v128.load8x8_u
kExprDrop,
kExprI32Const, 0xd4, 0xde, 0x94, 0xff, 0x00,  // i32.const
kExprEnd,  // end @247
]);

builder.addExport('main', 17);
let kBuiltins = { builtins: ['js-string', 'text-decoder', 'text-encoder'] };
const instance = builder.instantiate({}, kBuiltins);
