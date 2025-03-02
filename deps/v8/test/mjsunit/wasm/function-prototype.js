// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestFunctionPrototype() {
  var builder = new WasmModuleBuilder();

  var f = builder.addFunction("nine", kSig_i_v)
    .addBody([kExprI32Const, 9])
    .exportFunc();

  var func = builder.instantiate().exports.nine;

  // Check type and existence of prototype
  assertEquals('function', typeof func);
  assertEquals('function', typeof func.apply);
  assertEquals('prototype' in func, false);
  assertEquals(String(f.index), func.name);
  assertEquals(undefined, func.displayName);

  // Check that .apply() works.
  assertEquals(9, func.apply([]));
  assertEquals(9, func.apply([1]));
  assertEquals(9, func.apply([2, 3]));
  assertEquals(9, func.apply([6, 7, 9, 9]));

  // TODO(titzer): assertEquals(1, func.length);

  // Check we don't crash when converting to a string.
  print(func.toString());
})();
