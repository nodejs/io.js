// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function testExportedMain() {
  print("TestExportedMain...");
  var kReturnValue = 44;
  var builder = new WasmModuleBuilder();

  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprI32Const,
      kReturnValue,
      kExprReturn
    ])
    .exportFunc();

  var module = builder.instantiate();

  assertEquals("object", typeof module.exports);
  assertEquals("function", typeof module.exports.main);

  assertEquals(kReturnValue, module.exports.main());
})();

(function testExportedTwice() {
  print("TestExportedTwice...");
  var kReturnValue = 45;

  var builder = new WasmModuleBuilder();

  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprI32Const,
      kReturnValue,
      kExprReturn
    ])
    .exportAs("blah")
    .exportAs("foo");

  var module = builder.instantiate();

  assertEquals("object", typeof module.exports);
  assertEquals("function", typeof module.exports.blah);
  assertEquals("function", typeof module.exports.foo);

  assertEquals(kReturnValue, module.exports.foo());
  assertEquals(kReturnValue, module.exports.blah());
  assertSame(module.exports.blah, module.exports.foo);
})();

(function testEmptyName() {
  print("TestEmptyName...");
  var kReturnValue = 46;

  var builder = new WasmModuleBuilder();

  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprI32Const,
      kReturnValue,
      kExprReturn
    ])
    .exportAs("");

  var module = builder.instantiate();

  assertEquals("object", typeof module.exports);
  assertEquals("function", typeof module.exports[""]);

  assertEquals(kReturnValue, module.exports[""]());
})();

(function testNumericName() {
  print("TestNumericName...");
  var kReturnValue = 47;

  var builder = new WasmModuleBuilder();

  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprI32Const,
      kReturnValue,
      kExprReturn
    ])
    .exportAs("0");

  var module = builder.instantiate();

  assertEquals("object", typeof module.exports);
  assertEquals("function", typeof module.exports["0"]);

  assertEquals(kReturnValue, module.exports["0"]());
})();

(function testExportNameClash() {
  print("TestExportNameClash...");
  var builder = new WasmModuleBuilder();

  builder.addFunction("one",   kSig_v_v).addBody([kExprNop]).exportAs("main");
  builder.addFunction("two",   kSig_v_v).addBody([kExprNop]).exportAs("other");
  builder.addFunction("three", kSig_v_v).addBody([kExprNop]).exportAs("main");

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /Duplicate export name 'main' for function 0 and function 2/);
})();


(function testExportMultipleIdentity() {
  print("TestExportMultipleIdentity...");
  var builder = new WasmModuleBuilder();

  var f = builder.addFunction("one", kSig_v_v).addBody([kExprNop])
    .exportAs("a")
    .exportAs("b")
    .exportAs("c");

  let instance = builder.instantiate();
  let e = instance.exports;
  assertEquals("function", typeof e.a);
  assertEquals("function", typeof e.b);
  assertEquals("function", typeof e.c);
  assertSame(e.a, e.b);
  assertSame(e.a, e.c);
  assertEquals(String(f.index), e.a.name);
})();


(function testReexportJSMultipleIdentity() {
  print("TestReexportMultipleIdentity...");
  var builder = new WasmModuleBuilder();

  function js() {}

  var a = builder.addImport("m", "a", kSig_v_v);
  builder.addExport("f", a);
  builder.addExport("g", a);

  let instance = builder.instantiate({m: {a: js}});
  let e = instance.exports;
  assertEquals("function", typeof e.f);
  assertEquals("function", typeof e.g);
  assertFalse(e.f == js);
  assertFalse(e.g == js);
  assertTrue(e.f == e.g);
})();


(function testReexportJSMultiple() {
  print("TestReexportMultiple...");
  var builder = new WasmModuleBuilder();

  function js() {}

  var a = builder.addImport("q", "a", kSig_v_v);
  var b = builder.addImport("q", "b", kSig_v_v);
  builder.addExport("f", a);
  builder.addExport("g", b);

  let instance = builder.instantiate({q: {a: js, b: js}});
  let e = instance.exports;
  assertEquals("function", typeof e.f);
  assertEquals("function", typeof e.g);
  assertFalse(e.f == js);
  assertFalse(e.g == js);
  assertFalse(e.f == e.g);
})();
