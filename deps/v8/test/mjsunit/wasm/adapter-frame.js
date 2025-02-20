// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const JS = false;  // for testing the tests.
const WRONG1 = 0x0DEDFACE;
const WRONG2 = 0x0DEDBABE;
const WRONG3 = 0x0DEDD011

function makeSelect(type, args, which) {
  if (JS) {
    // For testing the tests.
    return function() {
      var val = +arguments[which];
      print("  " + val);
      if (type == kWasmI32) return val | 0;
      if (type == kWasmF32) return Math.fround(val);
      if (type == kWasmF64) return val;
      return undefined;
    }
  }

  var builder = new WasmModuleBuilder();
  var params = [];
  for (var i = 0; i < args; i++) params.push(type);
  builder.addFunction("select", makeSig(params, [type]))
    .addBody([kExprLocalGet, which])
    .exportFunc();

  return builder.instantiate().exports.select;
}

const inputs = [
  -1, 0, 2.2, 3.3, 3000.11, Infinity, -Infinity, NaN
];

(function TestInt1() {
  print("i32 1(0)...");
  var C = function(v) { return v | 0; }
  var select1 = makeSelect(kWasmI32, 1, 0);

  for (val of inputs) {
    assertEquals(C(val), select1(val));

    // under args
    assertEquals(C(undefined), select1());
    // over args
    assertEquals(C(val), select1(val, WRONG1));
    assertEquals(C(val), select1(val, WRONG1, WRONG2));
  }
})();

(function TestInt2() {
  print("i32 2(0)...");
  var C = function(v) { return v | 0; }
  var select = makeSelect(kWasmI32, 2, 0);

  for (val of inputs) {
    assertEquals(C(val), select(val, WRONG1));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(val), select(val));
    // over args
    assertEquals(C(val), select(val, WRONG1, WRONG2));
    assertEquals(C(val), select(val, WRONG1, WRONG2, WRONG3));
  }

  print("i32 2(1)...");
  var select = makeSelect(kWasmI32, 2, 1);

  for (val of inputs) {
    assertEquals(C(val), select(WRONG1, val));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(undefined), select(val));
    // over args
    assertEquals(C(val), select(WRONG1, val));
    assertEquals(C(val), select(WRONG1, val, WRONG2));
    assertEquals(C(val), select(WRONG1, val, WRONG2, WRONG3));
  }
})();

(function TestInt3() {
  print("i32 3(0)...");
  var C = function(v) { return v | 0; }
  var select = makeSelect(kWasmI32, 3, 0);

  for (val of inputs) {
    assertEquals(C(val), select(val, WRONG1, WRONG2));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(val), select(val));
    assertEquals(C(val), select(val, WRONG1));
    // over args
    assertEquals(C(val), select(val, WRONG1, WRONG2, WRONG3));
  }

  print("i32 3(1)...");
  var select = makeSelect(kWasmI32, 3, 1);

  for (val of inputs) {
    assertEquals(val | 0, select(WRONG1, val, WRONG2));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(undefined), select(0xDEDFACE));
    assertEquals(C(val), select(WRONG1, val));
    // over args
    assertEquals(C(val), select(WRONG1, val, WRONG2, WRONG3));
  }

  print("i32 3(2)...");
  var select = makeSelect(kWasmI32, 3, 2);

  for (val of inputs) {
    assertEquals(C(val), select(WRONG1, WRONG2, val));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(undefined), select(0xDEDFACE));
    assertEquals(C(undefined), select(WRONG1, WRONG2));
    // over args
    assertEquals(C(val), select(WRONG1, WRONG2, val, WRONG3));
  }
})();

(function TestFloat32_1() {
  print("f32 1(0)...");
  var C = function(v) { return Math.fround(v); }
  var select1 = makeSelect(kWasmF32, 1, 0);

  for (val of inputs) {
    assertEquals(C(val), select1(val));

    // under args
    assertEquals(C(undefined), select1());
    // over args
    assertEquals(C(val), select1(val, WRONG1));
    assertEquals(C(val), select1(val, WRONG1, WRONG2));
  }
})();

(function TestFloat32_2() {
  print("f32 2(0)...");
  var C = function(v) { return Math.fround(v); }
  var select = makeSelect(kWasmF32, 2, 0);

  for (val of inputs) {
    assertEquals(C(val), select(val, WRONG1));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(val), select(val));
    // over args
    assertEquals(C(val), select(val, WRONG1, WRONG2));
    assertEquals(C(val), select(val, WRONG1, WRONG2, WRONG3));
  }

  print("f32 2(1)...");
  var select = makeSelect(kWasmF32, 2, 1);

  for (val of inputs) {
    assertEquals(C(val), select(WRONG1, val));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(undefined), select(val));
    // over args
    assertEquals(C(val), select(WRONG1, val));
    assertEquals(C(val), select(WRONG1, val, WRONG2));
    assertEquals(C(val), select(WRONG1, val, WRONG2, WRONG3));
  }
})();

(function TestFloat32_2() {
  print("f32 3(0)...");
  var C = function(v) { return Math.fround(v); }
  var select = makeSelect(kWasmF32, 3, 0);

  for (val of inputs) {
    assertEquals(C(val), select(val, WRONG1, WRONG2));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(val), select(val));
    assertEquals(C(val), select(val, WRONG1));
    // over args
    assertEquals(C(val), select(val, WRONG1, WRONG2, WRONG3));
  }

  print("f32 3(1)...");
  var select = makeSelect(kWasmF32, 3, 1);

  for (val of inputs) {
    assertEquals(C(val), select(WRONG1, val, WRONG2));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(undefined), select(0xDEDFACE));
    assertEquals(C(val), select(WRONG1, val));
    // over args
    assertEquals(C(val), select(WRONG1, val, WRONG2, WRONG3));
  }

  print("f32 3(2)...");
  var select = makeSelect(kWasmF32, 3, 2);

  for (val of inputs) {
    assertEquals(C(val), select(WRONG1, WRONG2, val));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(undefined), select(0xDEDFACE));
    assertEquals(C(undefined), select(WRONG1, WRONG2));
    // over args
    assertEquals(C(val), select(WRONG1, WRONG2, val, WRONG3));
  }
})();


(function TestFloat64_1() {
  print("f64 1(0)...");
  var C = function(v) { return +v; }
  var select1 = makeSelect(kWasmF64, 1, 0);

  for (val of inputs) {
    assertEquals(C(val), select1(val));

    // under args
    assertEquals(C(undefined), select1());
    // over args
    assertEquals(C(val), select1(val, WRONG1));
    assertEquals(C(val), select1(val, WRONG1, WRONG2));
  }
})();

(function TestFloat64_2() {
  print("f64 2(0)...");
  var C = function(v) { return +v; }
  var select = makeSelect(kWasmF64, 2, 0);

  for (val of inputs) {
    assertEquals(C(val), select(val, WRONG1));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(val), select(val));
    // over args
    assertEquals(C(val), select(val, WRONG1, WRONG2));
    assertEquals(C(val), select(val, WRONG1, WRONG2, WRONG3));
  }

  print("f64 2(1)...");
  var select = makeSelect(kWasmF64, 2, 1);

  for (val of inputs) {
    assertEquals(C(val), select(WRONG1, val));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(undefined), select(val));
    // over args
    assertEquals(C(val), select(WRONG1, val));
    assertEquals(C(val), select(WRONG1, val, WRONG2));
    assertEquals(C(val), select(WRONG1, val, WRONG2, WRONG3));
  }
})();

(function TestFloat64_2() {
  print("f64 3(0)...");
  var C = function(v) { return +v; }
  var select = makeSelect(kWasmF64, 3, 0);

  for (val of inputs) {
    assertEquals(C(val), select(val, WRONG1, WRONG2));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(val), select(val));
    assertEquals(C(val), select(val, WRONG1));
    // over args
    assertEquals(C(val), select(val, WRONG1, WRONG2, WRONG3));
  }

  print("f64 3(1)...");
  var select = makeSelect(kWasmF64, 3, 1);

  for (val of inputs) {
    assertEquals(C(val), select(WRONG1, val, WRONG2));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(undefined), select(0xDEDFACE));
    assertEquals(C(val), select(WRONG1, val));
    // over args
    assertEquals(C(val), select(WRONG1, val, WRONG2, WRONG3));
  }

  print("f64 3(2)...");
  var select = makeSelect(kWasmF64, 3, 2);

  for (val of inputs) {
    assertEquals(C(val), select(WRONG1, WRONG2, val));

    // under args
    assertEquals(C(undefined), select());
    assertEquals(C(undefined), select(0xDEDFACE));
    assertEquals(C(undefined), select(WRONG1, WRONG2));
    // over args
    assertEquals(C(val), select(WRONG1, WRONG2, val, WRONG3));
  }
})();
