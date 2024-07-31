// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

let struct_index = builder.addStruct([makeField(kWasmI32, true)]);
let identical_struct_index = builder.addStruct([makeField(kWasmI32, true)]);
let distinct_struct_index = builder.addStruct([makeField(kWasmI64, true)]);

let struct_init = builder.addFunction("struct_init",
                                      makeSig([], [kWasmStructRef]))
    .addBody([kGCPrefix, kExprStructNewDefault, struct_index])
    .exportFunc();
let test_pass = builder.addFunction("test_pass",
                                    makeSig([kWasmStructRef], [kWasmI32]))
    .addBody([kExprLocalGet, 0,
              kGCPrefix, kExprRefTest, identical_struct_index])
    .exportFunc();
let test_fail = builder.addFunction("test_fail",
                                    makeSig([kWasmStructRef], [kWasmI32]))
    .addBody([kExprLocalGet, 0,
              kGCPrefix, kExprRefTest, distinct_struct_index])
    .exportFunc();

(function TestCanonicalizationSameInstance() {
  print(arguments.callee.name);
  let instance = builder.instantiate({});
  assertEquals(1, instance.exports.test_pass(instance.exports.struct_init()));
  assertEquals(0, instance.exports.test_fail(instance.exports.struct_init()));
})();

(function TestCanonicalizationSameModuleDifferentInstances() {
  print(arguments.callee.name);
  let module = builder.toModule();
  let instance1 = new WebAssembly.Instance(module, {});
  let instance2 = new WebAssembly.Instance(module, {});
  assertEquals(1, instance2.exports.test_pass(instance1.exports.struct_init()));
  assertEquals(0, instance2.exports.test_fail(instance1.exports.struct_init()));
})();

// GC between tests so that the type registry is cleared.
gc();

(function TestCanonicalizationDifferentModules() {
  print(arguments.callee.name);
  let instance1 = builder.instantiate({});
  let instance2 = builder.instantiate({});
  assertEquals(1, instance2.exports.test_pass(instance1.exports.struct_init()));
  assertEquals(0, instance2.exports.test_fail(instance1.exports.struct_init()));
})();

(function TestCanonicalizationDifferentModulesAfterGC() {
  print(arguments.callee.name);
  let struct = (function make_struct() {
    return builder.instantiate({}).exports.struct_init();
  })();
  // A the live {struct} object keeps the instance alive.
  gc();
  let instance = builder.instantiate({});
  assertEquals(1, instance.exports.test_pass(struct));
  assertEquals(0, instance.exports.test_fail(struct));
})();
