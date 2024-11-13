// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function testCallFFI(func, check) {
  var builder = new WasmModuleBuilder();

  var sig_index = builder.addType(kSig_i_dd);
  builder.addImport("", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,            // --
      kExprLocalGet, 1,            // --
      kExprCallFunction, 0  // --
    ])        // --
    .exportFunc();

  var main = builder.instantiate({"": {func: func}}).exports.main;

  for (var i = 0; i < 100000; i += 10003) {
    var a = 22.5 + i, b = 10.5 + i;
    var r = main(a, b);
    if (check) {
      check(r, a, b);
    }
  }
}

var global = (function() { return this; })();
var params = [-99, -99, -99, -99, -99];
var was_called = false;
var length = -1;

function FOREIGN_SUB(a, b) {
//  print("FOREIGN_SUB(" + a + ", " + b + ")");
  was_called = true;
  params[0] = this;
  params[1] = a;
  params[2] = b;
  return (a - b) | 0;
}

function check_FOREIGN_SUB(r, a, b) {
    assertEquals(a - b | 0, r);
    assertTrue(was_called);
//    assertEquals(global, params[0]);  // sloppy mode
    assertEquals(a, params[1]);
    assertEquals(b, params[2]);
    was_called = false;
}

// Test calling a normal JSFunction.
print("JSFunction");
testCallFFI(FOREIGN_SUB, check_FOREIGN_SUB);

// Test calling a proxy.
print("Proxy");
var proxy_sub = new Proxy(FOREIGN_SUB, {});
testCallFFI(proxy_sub, check_FOREIGN_SUB);

// Test calling a bind function.
print("Bind function");
var bind_sub = FOREIGN_SUB.bind();
testCallFFI(bind_sub, check_FOREIGN_SUB);

(function testCallConstructor() {
  print(arguments.callee.name);
  class C {}
  var builder = new WasmModuleBuilder();

  var sig_index = builder.addType(kSig_i_dd);
  builder.addImport("", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,            // --
      kExprLocalGet, 1,            // --
      kExprCallFunction, 0  // --
    ])        // --
    .exportFunc();

  let main_for_constructor_test = builder.instantiate({"": {func: C}}).exports.main;

  assertThrows(_ => main_for_constructor_test(12, 43), TypeError);
}) ();

(function testCallConstructorWithSuperClass() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_v_v);
  let func_index = builder.addImport('', 'func', sig_index);
  builder.addExport('exp', func_index);

  class B {}
  class C extends B {
    constructor() {
      super();
    }
  };
  let exports = builder.instantiate({'': {func: C}}).exports;
  assertThrows(_ => exports.exp(), TypeError);
})();

(function test_ffi_call_to_native() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();

  var sig_index = builder.addType(kSig_d_v);
  builder.addImport("", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprCallFunction, 0  // --
    ])        // --
    .exportFunc();

  var main = builder.instantiate({"": {func: Object.prototype.toString}}).exports.main;
  // The result of the call to Object.prototype.toString should be
  // [object Undefined]. However, we cannot test for this result because wasm
  // cannot return objects but converts them to float64 in this test.
  assertEquals(NaN, main());
})();

print("Callable JSObject");
function callable_target(a, b) { return a - b; }
let callable_obj = %GetCallable("callable_target");
testCallFFI(callable_obj, function check(r, a, b) {assertEquals(a - b, r);});

function FOREIGN_ABCD(a, b, c, d) {
//  print("FOREIGN_ABCD(" + a + ", " + b + ", " + c + ", " + d + ")");
  was_called = true;
  params[0] = this;
  params[1] = a;
  params[2] = b;
  params[3] = c;
  params[4] = d;
  return (a * b * 6) | 0;
}

function check_FOREIGN_ABCD(r, a, b) {
    assertEquals((a * b * 6) | 0, r);
    assertTrue(was_called);
//    assertEquals(global, params[0]);  // sloppy mode.
    assertEquals(a, params[1]);
    assertEquals(b, params[2]);
    assertEquals(undefined, params[3]);
    assertEquals(undefined, params[4]);
    was_called = false;
}

testCallFFI(FOREIGN_ABCD, check_FOREIGN_ABCD);

function FOREIGN_ARGUMENTS0() {
  print("FOREIGN_ARGUMENTS0");
  was_called = true;
  length = arguments.length;
  for (var i = 0; i < arguments.length; i++) {
    params[i] = arguments[i];
  }
  return (arguments[0] * arguments[1] * 7) | 0;
}

function FOREIGN_ARGUMENTS1(a) {
  print("FOREIGN_ARGUMENTS1", a);
  was_called = true;
  length = arguments.length;
  for (var i = 0; i < arguments.length; i++) {
    params[i] = arguments[i];
  }
  return (arguments[0] * arguments[1] * 7) | 0;
}

function FOREIGN_ARGUMENTS2(a, b) {
  print("FOREIGN_ARGUMENTS2", a, b);
  was_called = true;
  length = arguments.length;
  for (var i = 0; i < arguments.length; i++) {
    params[i] = arguments[i];
  }
  return (a * b * 7) | 0;
}

function FOREIGN_ARGUMENTS3(a, b, c) {
  print("FOREIGN_ARGUMENTS3", a, b, c);
  was_called = true;
  length = arguments.length;
  for (var i = 0; i < arguments.length; i++) {
    params[i] = arguments[i];
  }
  return (a * b * 7) | 0;
}

function FOREIGN_ARGUMENTS4(a, b, c, d) {
  print("FOREIGN_ARGUMENTS4", a, b, c, d);
  was_called = true;
  length = arguments.length;
  for (var i = 0; i < arguments.length; i++) {
    params[i] = arguments[i];
  }
  return (a * b * 7) | 0;
}

function check_FOREIGN_ARGUMENTS(r, a, b) {
  assertEquals((a * b * 7) | 0, r);
  assertTrue(was_called);
  assertEquals(2, length);
  assertEquals(a, params[0]);
  assertEquals(b, params[1]);
  was_called = false;
}

// Check a bunch of uses of the arguments object.
testCallFFI(FOREIGN_ARGUMENTS0, check_FOREIGN_ARGUMENTS);
testCallFFI(FOREIGN_ARGUMENTS1, check_FOREIGN_ARGUMENTS);
testCallFFI(FOREIGN_ARGUMENTS2, check_FOREIGN_ARGUMENTS);
testCallFFI(FOREIGN_ARGUMENTS3, check_FOREIGN_ARGUMENTS);
testCallFFI(FOREIGN_ARGUMENTS4, check_FOREIGN_ARGUMENTS);

function returnValue(val) {
  return function(a, b) {
    print("RETURN_VALUE ", val);
    return val;
  }
}


function checkReturn(expected) {
  return function(r, a, b) { assertEquals(expected, r); }
}

// Check that returning weird values doesn't crash
testCallFFI(returnValue(undefined), checkReturn(0));
testCallFFI(returnValue(null), checkReturn(0));
testCallFFI(returnValue("0"), checkReturn(0));
testCallFFI(returnValue("-77"), checkReturn(-77));

var objWithValueOf = {valueOf: function() { return 198; }}

testCallFFI(returnValue(objWithValueOf), checkReturn(198));

function testCallBinopVoid(type, func, check) {
  var passed_length = -1;
  var passed_a = -1;
  var passed_b = -1;
  var args_a = -1;
  var args_b = -1;

  ffi = {"": {func: function(a, b) {
    passed_length = arguments.length;
    passed_a = a;
    passed_b = b;
    args_a = arguments[0];
    args_b = arguments[1];
  }}};

  var builder = new WasmModuleBuilder();

  builder.addImport("", "func", makeSig_v_xx(type));
  builder.addFunction("main", makeSig_r_xx(kWasmI32, type))
    .addBody([
      kExprLocalGet, 0,            // --
      kExprLocalGet, 1,            // --
      kExprCallFunction, 0,        // --
      kExprI32Const, 33            // --
    ])                             // --
    .exportFunc()

  var main = builder.instantiate(ffi).exports.main;

  print("testCallBinopVoid", type);

  for (var i = 0; i < 100000; i += 10003.1) {
    var a = 22.5 + i, b = 10.5 + i;
    var r = main(a, b);
    assertEquals(33, r);
    assertEquals(2, passed_length);
    var expected_a, expected_b;
    switch (type) {
      case kWasmI32: {
        expected_a = a | 0;
        expected_b = b | 0;
        break;
      }
      case kWasmF32: {
        expected_a = Math.fround(a);
        expected_b = Math.fround(b);
        break;
      }
      case kWasmF64: {
        expected_a = a;
        expected_b = b;
        break;
      }
    }

    assertEquals(expected_a, args_a);
    assertEquals(expected_b, args_b);
    assertEquals(expected_a, passed_a);
    assertEquals(expected_b, passed_b);
  }
}


testCallBinopVoid(kWasmI32);
// TODO testCallBinopVoid(kWasmI64);
testCallBinopVoid(kWasmF32);
testCallBinopVoid(kWasmF64);

(function testCallPrint() {
  var builder = new WasmModuleBuilder();

  builder.addImport("", "print", makeSig_v_x(kWasmI32));
  builder.addImport("", "print", makeSig_v_x(kWasmF64));
  builder.addFunction("main", makeSig_v_x(kWasmF64))
    .addBody([
      kExprI32Const, 37,     // --
      kExprCallFunction, 0,  // --
      kExprLocalGet, 0,      // --
      kExprCallFunction, 1   // --
    ])                       // --
    .exportFunc()

  var main = builder.instantiate({"": {print: print}}).exports.main;
  for (var i = -9; i < 900; i += 6.125) main(i);
})();


(function testImportNumbers() {
  print("TestImportNumbers...");
  var builder = new WasmModuleBuilder();

  builder.addImport("", '0', kSig_v_i);

  builder.instantiate({"": {0: print}});
})();

(function testImportNumbers2() {
  print("TestImportNumbers2...");
  var builder = new WasmModuleBuilder();

  builder.addImport('foo', '0', kSig_v_i);
  builder.addImport('0', 'foo', kSig_v_i);
  builder.addImport('0', '0', kSig_v_i);
  builder.addImport('18', '-3', kSig_v_i);
  builder.addImport('-3', '18', kSig_v_i);

  builder.instantiate({
    foo: {0: print},
    0: {0: print, foo: print},
    18: {'-3': print},
    '-3': {18: print}
  });
})();

(function ImportSymbolAsVoidDoesNotThrow() {
  print("ImportSymbolAsVoidDoesNotThrow...");
  var builder = new WasmModuleBuilder();
  // Return type is void, so there should be no ToNumber conversion.
  var index = builder.addImport("", "func", kSig_v_v);
  builder.addFunction("main", kSig_v_v)
      .addBody([kExprCallFunction, 0])
      .exportFunc();
  var func = () => Symbol();
  var main = builder.instantiate({"": {func: func}}).exports.main;
  main();
})();

(function ToNumberCalledOnImport() {
  var builder = new WasmModuleBuilder();
  // Return type is int, so there should be a ToNumber conversion.
  var index = builder.addImport("", "func", kSig_i_v);
  builder.addFunction("main", kSig_i_v)
      .addBody([kExprCallFunction, 0])
      .exportFunc();
  var num_valueOf = 0;
  function Foo() {}
  Foo.prototype.valueOf = () => ++num_valueOf;
  var func = () => new Foo();
  var main = builder.instantiate({"": {func: func}}).exports.main;
  main();
  assertEquals(1, num_valueOf);
  main();
  assertEquals(2, num_valueOf);
})();

(function ToNumberNotCalledOnVoidImport() {
  var builder = new WasmModuleBuilder();
  // Return type is void, so there should be no ToNumber conversion.
  var index = builder.addImport("", "func", kSig_v_v);
  builder.addFunction("main", kSig_v_v)
      .addBody([kExprCallFunction, 0])
      .exportFunc();
  var num_valueOf = 0;
  function Foo() {}
  Foo.prototype.valueOf = () => ++num_valueOf;
  var func = () => new Foo();
  var main = builder.instantiate({"": {func: func}}).exports.main;
  main();
  main();
  assertEquals(0, num_valueOf);
})();

(function ImportWithCustomGetter() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addImport("import", "func", kSig_v_v);

  const ffi = {};
  Object.defineProperty(ffi, 'import', {
    get: _ => {
      return {func: () => null };
    }
  });

  builder.instantiate(ffi);
})();
