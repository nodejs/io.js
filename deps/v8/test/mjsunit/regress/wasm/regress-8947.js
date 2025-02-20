// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function testCallReexportedJSFunc() {
 print(arguments.callee.name);

  function dothrow() {
    throw "exception";
  }

  var builder = new WasmModuleBuilder();
  const imp_index = builder.addImport("w", "m", kSig_i_v);
  builder.addExport("exp", imp_index);
  var exp = builder.instantiate({w: {m: dothrow}}).exports.exp;

  builder.addImport("w", "m", kSig_i_v);
  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprCallFunction, 0, // --
    ])                             // --
    .exportFunc();

  var main = builder.instantiate({w: {m: exp}}).exports.main;
  assertThrowsEquals(main, "exception");
})();

(function testCallReexportedAPIFunc() {
 print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  const imp_index = builder.addImport("w", "m", kSig_i_v);
  builder.addExport("exp", imp_index);
  var exp = builder.instantiate({w: {m: WebAssembly.Module}}).exports.exp;

  builder.addImport("w", "m", kSig_i_v);
  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprCallFunction, 0, // --
    ])                             // --
    .exportFunc();

  var main = builder.instantiate({w: {m: exp}}).exports.main;
  assertThrows(main, TypeError);
})();
