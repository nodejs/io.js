// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function toBytes(string) {
  var a = new Array(string.length + 1);
  a[0] = string.length;
  for (i = 0; i < string.length; i++) {
    a[i + 1] = string.charCodeAt(i);
  }
  return a;
}

(function TestEmptyNamesSection() {
  print('TestEmptyNamesSection...');
  var builder = new WasmModuleBuilder();

  builder.addExplicitSection([kUnknownSectionCode, 6, ...toBytes('name'), 0]);

  var buffer = builder.toBuffer();
  assertTrue(WebAssembly.validate(buffer));
  assertTrue((new WebAssembly.Module(buffer)) instanceof WebAssembly.Module);
})();

(function TestTruncatedNamesSection() {
  print('TestTruncatedNamesSection...');
  var builder = new WasmModuleBuilder();

  builder.addExplicitSection([kUnknownSectionCode, 6, ...toBytes('name'), 1]);

  var buffer = builder.toBuffer();
  assertTrue(WebAssembly.validate(buffer));
  assertTrue((new WebAssembly.Module(buffer)) instanceof WebAssembly.Module);
})();

(function TestBrokenNamesSection() {
  print('TestBrokenNamesSection...');
  var builder = new WasmModuleBuilder();

  builder.addExplicitSection(
      [kUnknownSectionCode, 7, ...toBytes('name'), 1, 100]);

  var buffer = builder.toBuffer();
  assertTrue(WebAssembly.validate(buffer));
  assertTrue((new WebAssembly.Module(buffer)) instanceof WebAssembly.Module);
})();
