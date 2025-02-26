// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let importingModuleBinary1 = (() => {
  var builder = new WasmModuleBuilder();
  builder.addImport('', 'f', kSig_i_v);
  return new Int8Array(builder.toBuffer());
})();

let importingModuleBinary2 = (() => {
  var builder = new WasmModuleBuilder();
  builder.addImport('', 'f', kSig_i_v);
  builder.addFunction('g', kSig_v_v)
    .addBody([kExprNop]);
  return new Int8Array(builder.toBuffer());
})();

let importingModuleBinary3 = (() => {
  var builder = new WasmModuleBuilder();
  builder.addImport('', 'f', kSig_i_v);
  builder.addImport('', 'f2', kSig_i_v);
  builder.addFunction('g', kSig_v_v)
    .addBody([kExprNop]);
  return new Int8Array(builder.toBuffer());
})();

let importingModuleBinary4 = (() => {
  var builder = new WasmModuleBuilder();
  builder.addFunction('g', kSig_v_v)
    .addBody([kExprNop]);
  return new Int8Array(builder.toBuffer());
})();

const complexImportingModuleBinary1 = (() => {
  let builder = new WasmModuleBuilder();

  builder.addImport('a', 'b', kSig_v_v);
  builder.addImportedMemory('c', 'd', 1);
  builder.addImportedTable('e', 'f', 1);
  builder.addImportedGlobal('g', '⚡', kWasmI32);
  builder.addFunction('g', kSig_v_v)
    .addBody([kExprNop]);
  return builder.toBuffer();
})();

const complexImportingModuleBinary2 = (() => {
  let builder = new WasmModuleBuilder();

  builder.addImport('a', 'b', kSig_v_v);
  builder.addImportedMemory('c', 'd', 1);
  builder.addImportedTable('e', 'f', 1);
  builder.addImportedGlobal('g', '⚡', kWasmI32);
  builder.addFunction('g', kSig_v_v)
    .addBody([kExprNop]);
  return builder.toBuffer();
})();

var tests = [
  importingModuleBinary1,
  importingModuleBinary2,
  importingModuleBinary3,
  importingModuleBinary4,
  complexImportingModuleBinary1,
  complexImportingModuleBinary2
];

for (var index in tests) {
  assertPromiseResult(
    WebAssembly.compile(tests[index]),
    m => assertTrue(m instanceof WebAssembly.Module),
    assertUnreachable);
}
