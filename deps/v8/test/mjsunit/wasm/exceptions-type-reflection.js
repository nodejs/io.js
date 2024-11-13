// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-type-reflection

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let testcases = [
{types: {parameters:[]}, sig: kSig_v_v},
{types: {parameters:["i32"]}, sig: kSig_v_i},
{types: {parameters:["i64"]}, sig: kSig_v_l},
{types: {parameters:["f64", "f64", "i32"]}, sig: kSig_v_ddi},
{types: {parameters:["f32"]}, sig: kSig_v_f},
];

(function TestExport() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  testcases.forEach(function(expected, i) {
    let except = builder.addTag(expected.sig);
    builder.addExportOfKind("ex" + i, kExternalTag, except);
  });

  let instance = builder.instantiate();
  testcases.forEach(function(expected, i) {
    assertEquals(instance.exports["ex" + i].type(), expected.types);
  });
})();

(function TestImportExport() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let imports = {m: {}};

  testcases.forEach(function(expected, i) {
    let t = new WebAssembly.Tag(expected.types);
    let index = builder.addImportedTag("m", "ex" + i, expected.sig);
    builder.addExportOfKind("ex" + i, kExternalTag, index);
    imports.m["ex" + i] = t;
  });

  let instance = builder.instantiate(imports);
  testcases.forEach(function(expected, i) {
    assertEquals(instance.exports["ex" + i].type(), expected.types);
  })
})();

(function TestJSTag() {
  print(arguments.callee.name);
  assertEquals(WebAssembly.JSTag.type(), {parameters:['externref']});
})();
