// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function testCallImport(func, check) {
  var builder = new WasmModuleBuilder();

  var sig_index = builder.addType(kSig_i_dd);
  builder.addImport("q", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,            // --
      kExprLocalGet, 1,            // --
      kExprCallFunction, 0])         // --
    .exportAs("main");

  var main = builder.instantiate({q: {func: func}}).exports.main;

  for (var i = 0; i < 100000; i += 10003) {
    var a = 22.5 + i, b = 10.5 + i;
    var r = main(a, b);
    check(r, a, b);
  }
}

var global = (function() { return this; })();
var params = [-99, -99, -99, -99];
var was_called = false;
var length = -1;

function FOREIGN_SUB(a, b) {
  print("FOREIGN_SUB(" + a + ", " + b + ")");
  was_called = true;
  params[0] = this;
  params[1] = a;
  params[2] = b;
  return (a - b) | 0;
}

function check_FOREIGN_SUB(r, a, b) {
    assertEquals(a - b | 0, r);
    assertTrue(was_called);
    assertEquals(global, params[0]);  // sloppy mode
    assertEquals(a, params[1]);
    assertEquals(b, params[2]);
    was_called = false;
}

testCallImport(FOREIGN_SUB, check_FOREIGN_SUB);


function FOREIGN_ABCD(a, b, c, d) {
  print("FOREIGN_ABCD(" + a + ", " + b + ", " + c + ", " + d + ")");
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
    assertEquals(global, params[0]);  // sloppy mode.
    assertEquals(a, params[1]);
    assertEquals(b, params[2]);
    assertEquals(undefined, params[3]);
    assertEquals(undefined, params[4]);
    was_called = false;
}

testCallImport(FOREIGN_ABCD, check_FOREIGN_ABCD);

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
testCallImport(FOREIGN_ARGUMENTS0, check_FOREIGN_ARGUMENTS);
testCallImport(FOREIGN_ARGUMENTS1, check_FOREIGN_ARGUMENTS);
testCallImport(FOREIGN_ARGUMENTS2, check_FOREIGN_ARGUMENTS);
testCallImport(FOREIGN_ARGUMENTS3, check_FOREIGN_ARGUMENTS);
testCallImport(FOREIGN_ARGUMENTS4, check_FOREIGN_ARGUMENTS);

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
testCallImport(returnValue(undefined), checkReturn(0));
testCallImport(returnValue(null), checkReturn(0));
testCallImport(returnValue("0"), checkReturn(0));
testCallImport(returnValue("-77"), checkReturn(-77));

var objWithValueOf = {valueOf: function() { return 198; }}

testCallImport(returnValue(objWithValueOf), checkReturn(198));


function testCallBinopVoid(type, func, check) {
  var passed_length = -1;
  var passed_a = -1;
  var passed_b = -1;
  var args_a = -1;
  var args_b = -1;

  var ffi = {q: {func: function(a, b) {
    passed_length = arguments.length;
    passed_a = a;
    passed_b = b;
    args_a = arguments[0];
    args_b = arguments[1];
  }}};

  var builder = new WasmModuleBuilder();

  builder.addImport("q", "func", makeSig_v_xx(type));
  builder.addFunction("main", makeSig_r_xx(kWasmI32, type))
    .addBody([
      kExprLocalGet, 0,           // --
      kExprLocalGet, 1,           // --
      kExprCallFunction, 0,       // --
      kExprI32Const, 39,          // --
    ])
    .exportFunc("main");

  var main = builder.instantiate(ffi).exports.main;

  print("testCallBinopVoid", type);

  for (var i = 0; i < 100000; i += 10003.1) {
    var a = 22.5 + i, b = 10.5 + i;
    var r = main(a, b);
    assertEquals(39, r);
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



function testCallPrint() {
  var builder = new WasmModuleBuilder();
  builder.addImport("q", "print", makeSig_v_x(kWasmI32));
  builder.addImport("q", "print", makeSig_r_x(kWasmF64, kWasmF64));
  builder.addFunction("main", makeSig_r_x(kWasmF64, kWasmF64))
    .addBody([
      kExprI32Const, 27,     // --
      kExprCallFunction, 0,  // --
      kExprLocalGet, 0,      // --
      kExprCallFunction, 1   // --
    ])
    .exportFunc();

  var main = builder.instantiate({q: {print: print}}).exports.main;

  for (var i = -9; i < 900; i += 16.125) {
    main(i);
  }
}

testCallPrint();
testCallPrint();


function testCallImport2(foo, bar, expected) {
  var builder = new WasmModuleBuilder();

  builder.addImport("q", "foo", kSig_i_v);
  builder.addImport("t", "bar", kSig_i_v);
  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprCallFunction, 0, // --
      kExprCallFunction, 1, // --
      kExprI32Add,                 // --
    ])                             // --
    .exportFunc();

  var main = builder.instantiate({q: {foo: foo}, t: {bar: bar}}).exports.main;
  assertEquals(expected, main());
}

testCallImport2(function() { return 33; }, function () { return 44; }, 77);


function testImportName(name) {
  var builder = new WasmModuleBuilder();
  builder.addImport("M", name, kSig_i_v);
  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprCallFunction, 0
    ])
    .exportFunc();

  let main = builder.instantiate({M: {[name]: () => 42}}).exports.main;
  assertEquals(42, main());
}

testImportName('bla');
testImportName('0');
testImportName('  a @#$2 324 ');
testImportName('');

(function testExportedImportsOnDifferentInstances() {
  print(arguments.callee.name);
  const exp = (function() {
    const builder = new WasmModuleBuilder();
    builder.addFunction('f11', kSig_i_v)
        .addBody([kExprI32Const, 11])
        .exportFunc();
    builder.addFunction('f17', kSig_i_v)
        .addBody([kExprI32Const, 17])
        .exportFunc();
    return builder.instantiate().exports;
  })();

  const builder = new WasmModuleBuilder();
  const imp_index = builder.addImport('q', 'imp', kSig_i_v);
  builder.addExport('exp', imp_index);

  const module = builder.toModule();
  const instance0 = new WebAssembly.Instance(module, {q: {imp: exp.f11}});
  const instance1 = new WebAssembly.Instance(module, {q: {imp: exp.f17}});
  const instance2 = new WebAssembly.Instance(module, {q: {imp: _ => 21}});
  const instance3 = new WebAssembly.Instance(module, {q: {imp: _ => 27}});

  assertEquals(11, instance0.exports.exp());
  assertEquals(17, instance1.exports.exp());
  assertEquals(21, instance2.exports.exp());
  assertEquals(27, instance3.exports.exp());
})();

(function testImportedStartFunctionOnDifferentInstances() {
  print(arguments.callee.name);
  var global = 0;
  const set_global = n => global = n;
  const exp = (function() {
    const builder = new WasmModuleBuilder();
    const imp_index = builder.addImport('q', 'imp', kSig_v_i);
    builder.addFunction('f11', kSig_v_v)
        .addBody([kExprI32Const, 11, kExprCallFunction, imp_index])
        .exportFunc();
    builder.addFunction('f17', kSig_v_v)
        .addBody([kExprI32Const, 17, kExprCallFunction, imp_index])
        .exportFunc();
    return builder.instantiate({q: {imp: set_global}}).exports;
  })();

  const builder = new WasmModuleBuilder();
  const imp_index = builder.addImport('q', 'imp', kSig_v_v);
  builder.addStart(imp_index);

  const module = builder.toModule();

  assertEquals(0, global);
  new WebAssembly.Instance(module, {q: {imp: exp.f11}});
  assertEquals(11, global);
  new WebAssembly.Instance(module, {q: {imp: exp.f17}});
  assertEquals(17, global);
  new WebAssembly.Instance(module, {q: {imp: _ => set_global(21)}});
  assertEquals(21, global);
  new WebAssembly.Instance(module, {q: {imp: _ => set_global(27)}});
  assertEquals(27, global);
})();

(function testImportedStartFunctionUsesRightInstance() {
  print(arguments.callee.name);
  var global = 0;
  const set_global = n => global = n;
  const exp = (function() {
    const builder = new WasmModuleBuilder();
    builder.addMemory(1, 1);
    builder.exportMemoryAs('mem');
    const imp_index = builder.addImport('q', 'imp', kSig_v_i);
    builder.addFunction('f', kSig_v_v)
        .addBody([kExprI32Const, 0, kExprI32Const, 11, kExprI32StoreMem8, 0, 0])
        .exportFunc();
    return builder.instantiate({q: {imp: set_global}}).exports;
  })();

  const builder = new WasmModuleBuilder();
  const imp_index = builder.addImport('q', 'imp', kSig_v_v);
  builder.addStart(imp_index);
  const module = builder.toModule();

  assertEquals(0, new Uint8Array(exp.mem.buffer)[0], 'memory initially 0');
  new WebAssembly.Instance(module, {q: {imp: exp.f}});
  assertEquals(11, new Uint8Array(exp.mem.buffer)[0], 'memory changed to 11');
})();

(function testImportWrapperWorksWithStrictAndSloppy() {
  function jsSloppyMatch(x) {
    assertSame(globalThis, this);
    return x - 4;
  }

  function jsStrictMatch(x) {
    "use strict";

    assertEquals(undefined, this);
    return x - 4;
  }

  function jsSloppyMismatch(x, y) {
    assertSame(globalThis, this);
    assertEquals(undefined, y);
    return x - 4;
  }

  function jsStrictMismatch(x, y) {
    "use strict";

    assertEquals(undefined, this);
    assertEquals(undefined, y);
    return x - 4;
  }

  const builder = new WasmModuleBuilder();
  const functionIndex = builder.addImport("imports", "jsFun", kSig_i_i);
  builder.addFunction('wasmFun', kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprI32Const, 2,
      kExprI32Mul,
      kExprCallFunction, functionIndex
    ])
    .exportFunc();
  const module = builder.toModule();

  print("Running matching sloppy function");
  const instanceSloppyMatch = new WebAssembly.Instance(module, {imports: {jsFun: jsSloppyMatch}});
  assertEquals(42, instanceSloppyMatch.exports.wasmFun(23));

  print("Running matching strict function");
  const instanceStrictMatch = new WebAssembly.Instance(module, {imports: {jsFun: jsStrictMatch}});
  assertEquals(42, instanceStrictMatch.exports.wasmFun(23));

  print("Running mismatching sloppy function");
  const instanceSloppyMismatch = new WebAssembly.Instance(module, {imports: {jsFun: jsSloppyMismatch}});
  assertEquals(42, instanceSloppyMismatch.exports.wasmFun(23));

  print("Running mismatching strict function");
  const instanceStrictMismatch = new WebAssembly.Instance(module, {imports: {jsFun: jsStrictMismatch}});
  assertEquals(42, instanceStrictMismatch.exports.wasmFun(23));
})();
