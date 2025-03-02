// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function testAddGetter(object, name, val) {
  Object.defineProperty(object, name, { get: function() { return val; } });
  assertSame(val, object[name]);
}

function testAddGetterFails(object, name, val) {
  function assign() {
    Object.defineProperty(object, name, { get: function() { return val; } });
  }
  assertThrows(assign, TypeError);
}

function testAddGetterBothWays(object, name, val) {
  print("Object.defineProperty");
  Object.defineProperty(object, name, { get: function() { return val; } });
  print("object.__defineGetter__");
  object.__defineGetter__(name, () => val);
  assertSame(val, object[name]);
}

function testFailToAddGetter(object, name, val) {
  assertThrows(() => Object.defineProperty(object, name, { get: function() { return val; } }));
}

testAddGetter(testAddGetter, "name", 11);

function makeBuilder() {
  var builder = new WasmModuleBuilder();

  builder.addFunction("f", kSig_v_v)
    .addBody([])
    .exportFunc();
  return builder;
}

(function TestAddGetterToFunction() {
  print("TestAddGetterToFunction...");
  var builder = makeBuilder();
  var f = builder.instantiate().exports.f;
  testAddGetterBothWays(f, "name", "foo");
  testAddGetter(f, "blam", "baz");
})();

(function TestAddGetterToModule() {
  print("TestAddGetterToModule...");
  var builder = makeBuilder();
  var module = new WebAssembly.Module(builder.toBuffer());
  testAddGetter(module, "exports", 290);
  testAddGetter(module, "xyz", new Object());
})();

(function TestAddGetterToInstance() {
  print("TestAddGetterToInstance...");
  var builder = makeBuilder();
  var instance = builder.instantiate();
  testAddGetter(instance, "exports", 290);
  testAddGetter(instance, "xyz", new Object());
})();

(function TestAddGetterToExports() {
  print("TestAddGetterToExports...");
  var builder = makeBuilder();
  var exports = builder.instantiate().exports;
  testFailToAddGetter(exports, "f", 9834);
  testAddGetterFails(exports, "nag", new Number(2));
})();
