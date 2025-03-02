// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let kRefExtern = wasmRefType(kWasmExternRef);
let kSig_e_v = makeSig([], [kRefExtern]);

// Part I: Test that the String builtins throw when called
// with arguments of incorrect types.

let length = 3;
let instance = (() => {
  let builder = new WasmModuleBuilder();
  builder.startRecGroup();
  let good_array_i16 = builder.addArray(kWasmI16, true, kNoSuperType, true);
  builder.endRecGroup();
  builder.startRecGroup();
  let good_array_i8 = builder.addArray(kWasmI8, true, kNoSuperType, true);
  builder.endRecGroup();
  builder.startRecGroup();
  let bad_array_i16 = builder.addArray(kWasmI16, true, kNoSuperType, true);
  let bad_array_i8 = builder.addArray(kWasmI8, true, kNoSuperType, true);
  builder.endRecGroup();

  let wtf16_data = builder.addPassiveDataSegment([97, 0, 98, 0, 99, 0]);
  let wtf8_data = builder.addPassiveDataSegment([97, 98, 99]);

  let use_i16_array = builder.addImport(
      'wasm:js-string', 'fromCharCodeArray',
      makeSig([wasmRefType(good_array_i16), kWasmI32, kWasmI32], [kRefExtern]));
  let use_i8_array = builder.addImport(
      'wasm:text-decoder', 'decodeStringFromUTF8Array',
      makeSig([wasmRefType(good_array_i8), kWasmI32, kWasmI32], [kRefExtern]));

  builder.addExport('use_i16_array', use_i16_array);
  builder.addExport('use_i8_array', use_i8_array);

  builder.addFunction(
      "bad_i16_array", makeSig([], [wasmRefType(bad_array_i16)]))
    .exportFunc()
    .addBody([
      kExprI32Const, 0, kExprI32Const, length,
      kGCPrefix, kExprArrayNewData, bad_array_i16, wtf16_data
    ]);

  builder.addFunction(
      "good_i16_array", makeSig([], [wasmRefType(good_array_i16)]))
    .exportFunc()
    .addBody([
      kExprI32Const, 0, kExprI32Const, length,
      kGCPrefix, kExprArrayNewData, good_array_i16, wtf16_data
    ]);

  builder.addFunction(
      "bad_i8_array", makeSig([], [wasmRefType(bad_array_i8)]))
    .exportFunc()
    .addBody([
      kExprI32Const, 0, kExprI32Const, length,
      kGCPrefix, kExprArrayNewData, bad_array_i8, wtf8_data
    ]);

  builder.addFunction(
      "good_i8_array", makeSig([], [wasmRefType(good_array_i8)]))
    .exportFunc()
    .addBody([
      kExprI32Const, 0, kExprI32Const, length,
      kGCPrefix, kExprArrayNewData, good_array_i8, wtf8_data
    ]);

  return builder.instantiate({}, {builtins: ['js-string', 'text-decoder']});
})();

let good_a16 = instance.exports.good_i16_array();
let bad_a16 = instance.exports.bad_i16_array();
let good_a8 = instance.exports.good_i8_array();
let bad_a8 = instance.exports.bad_i8_array();

assertEquals("abc", instance.exports.use_i16_array(good_a16, 0, length));
assertEquals("abc", instance.exports.use_i8_array(good_a8, 0, length));

assertThrows(() => instance.exports.use_i16_array(bad_a16, 0, length),
             TypeError);
assertThrows(() => instance.exports.use_i8_array(bad_a8, 0, length),
             TypeError);

// Part II: Test that instantiating the module throws a CompileError when the
// string imports use incorrect types.

let array_i16;
let array_i8;
let good_array_i8;

function MakeInvalidImporterBuilder() {
  let builder = new WasmModuleBuilder();
  builder.startRecGroup();
  array_i16 = builder.addArray(kWasmI16, true, kNoSuperType, true);
  array_i8 = builder.addArray(kWasmI8, true, kNoSuperType, true);
  builder.endRecGroup();
  builder.startRecGroup();
  good_array_i8 = builder.addArray(kWasmI8, true, kNoSuperType, true);
  builder.endRecGroup();
  return builder;
}

let b1 = MakeInvalidImporterBuilder();
let b2 = MakeInvalidImporterBuilder();
let b3 = MakeInvalidImporterBuilder();
let b4 = MakeInvalidImporterBuilder();
let b5 = MakeInvalidImporterBuilder();
let b6 = MakeInvalidImporterBuilder();
let b99 = MakeInvalidImporterBuilder();

let array16ref = wasmRefNullType(array_i16);
let array8ref = wasmRefNullType(array_i8);

// These are invalid because they use array types with the right element
// type but violating the single-element-recgroup requirement.
b1.addImport('wasm:js-string', 'fromCharCodeArray',
             makeSig([array16ref, kWasmI32, kWasmI32], [kRefExtern]));
b2.addImport('wasm:text-decoder', 'decodeStringFromUTF8Array',
             makeSig([array8ref, kWasmI32, kWasmI32], [kRefExtern]));
b3.addImport('wasm:js-string', 'intoCharCodeArray',
             makeSig([kWasmExternRef, array16ref, kWasmI32], [kWasmI32]));
b4.addImport('wasm:text-encoder', 'encodeStringIntoUTF8Array',
             makeSig([kWasmExternRef, array8ref, kWasmI32], [kWasmI32]));
b5.addImport('wasm:text-encoder', 'encodeStringToUTF8Array',
             makeSig([kWasmExternRef], [wasmRefType(array_i8)]));
// This is invalid because the return type is nullable.
b6.addImport('wasm:text-encoder', 'encodeStringToUTF8Array',
             makeSig([kWasmExternRef], [wasmRefNullType(good_array_i8)]));
// One random example of a non-array-related incorrect type (incorrect result).
b99.addImport('wasm:js-string', 'charCodeAt',
             makeSig([kWasmExternRef, kWasmI32], [kWasmI64]));

let kBuiltins = { builtins: ['js-string', 'text-encoder', 'text-decoder'] };
assertThrows(() => b1.instantiate({}, kBuiltins), WebAssembly.CompileError);
assertThrows(() => b2.instantiate({}, kBuiltins), WebAssembly.CompileError);
assertThrows(() => b3.instantiate({}, kBuiltins), WebAssembly.CompileError);
assertThrows(() => b4.instantiate({}, kBuiltins), WebAssembly.CompileError);
assertThrows(() => b5.instantiate({}, kBuiltins), WebAssembly.CompileError);
assertThrows(() => b6.instantiate({}, kBuiltins), WebAssembly.CompileError);
assertThrows(() => b99.instantiate({}, kBuiltins), WebAssembly.CompileError);

(function () {
  let bytes = b99.toBuffer();
  assertTrue(WebAssembly.validate(bytes));
  // All ways to specify compile-time imports agree that one import has
  // an invalid signature.
  // (1) new WebAssembly.Module
  assertThrows(
    () => new WebAssembly.Module(bytes, kBuiltins), WebAssembly.CompileError);
  // (2) WebAssembly.validate
  assertFalse(WebAssembly.validate(bytes, kBuiltins));
  // (3) WebAssembly.compile
  assertThrowsAsync(
      WebAssembly.compile(bytes, kBuiltins), WebAssembly.CompileError);
  // (4) WebAssembly.instantiate
  assertThrowsAsync(
    WebAssembly.instantiate(bytes, {}, kBuiltins), WebAssembly.CompileError);

  // For compileStreaming/instantiateStreaming, see separate test.
})();
