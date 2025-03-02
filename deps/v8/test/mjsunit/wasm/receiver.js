// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function testCallImport(func, expected, a, b) {
  var builder = new WasmModuleBuilder();

  var sig_index = builder.addType(kSig_i_dd);
  builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,            // --
      kExprLocalGet, 1,            // --
      kExprCallFunction, 0])         // --
    .exportAs("main");

  var main = builder.instantiate({mod: {func: func}}).exports.main;

  assertEquals(expected, main(a, b));
}

var global = (function() { return this; })();

function sloppyReceiver(a, b) {
  assertEquals(global, this);
  assertEquals(33.3, a);
  assertEquals(44.4, b);
  return 11;
}

function strictReceiver(a, b) {
  'use strict';
  assertEquals(undefined, this);
  assertEquals(55.5, a);
  assertEquals(66.6, b);
  return 22;
}

testCallImport(sloppyReceiver, 11, 33.3, 44.4);
testCallImport(strictReceiver, 22, 55.5, 66.6);
