// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function testAnyFuncIdentityFunction() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_a_a)
      .addBody([kExprLocalGet, 0])
      .exportFunc();

  const instance = builder.instantiate();

  assertThrows(() => instance.exports.main(print), TypeError);
  assertThrows(() => instance.exports.main({'hello': 'world'}), TypeError);
  assertSame(
      instance.exports.main, instance.exports.main(instance.exports.main));
})();

(function testPassAnyFuncToImportedFunction() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const sig_index = builder.addType(kSig_v_a);
  const imp_index = builder.addImport('q', 'func', sig_index);
  builder.addFunction('main', sig_index)
      .addBody([kExprLocalGet, 0, kExprCallFunction, imp_index])
      .exportFunc();

  const main = builder.instantiate({q: {func: checkFunction}}).exports.main;

  function checkFunction(value) {
    assertSame(main, value);
  }

  main(main);
})();

(function testPassAnyFuncWithGCWithLocals() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const ref_sig = builder.addType(kSig_v_a);
  const void_sig = builder.addType(kSig_v_v);
  const imp_index = builder.addImport('q', 'func', ref_sig);
  const gc_index = builder.addImport('q', 'gc', void_sig);
  // First call the gc, then check if the object still exists.
  builder.addFunction('main', ref_sig)
      .addLocals(kWasmAnyFunc, 10)
      .addBody([
        kExprLocalGet,     0,
        kExprLocalSet,     1,  // Set local
        kExprLocalGet,     0,
        kExprLocalSet,     2,  // Set local
        kExprLocalGet,     0,
        kExprLocalSet,     3,  // Set local
        kExprLocalGet,     0,
        kExprLocalSet,     4,  // Set local
        kExprLocalGet,     0,
        kExprLocalSet,     5,  // Set local
        kExprLocalGet,     0,
        kExprLocalSet,     6,  // Set local
        kExprLocalGet,     0,
        kExprLocalSet,     7,  // Set local
        kExprLocalGet,     0,
        kExprLocalSet,     8,  // Set local
        kExprLocalGet,     0,
        kExprLocalSet,     9,  // Set local
        kExprLocalGet,     0,
        kExprLocalSet,     10,        // Set local
        kExprCallFunction, gc_index,  // call gc
        kExprLocalGet,     9,
        kExprCallFunction, imp_index  // call import
      ])
      .exportFunc();

  const main =
      builder.instantiate({q: {func: checkFunction, gc: gc}}).exports.main;

  function checkFunction(value) {
    assertSame(main, value);
  }

  main(main);
})();

(function testPassAnyFuncWithGC() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const ref_sig = builder.addType(kSig_v_a);
  const void_sig = builder.addType(kSig_v_v);
  const imp_index = builder.addImport('q', 'func', ref_sig);
  const gc_index = builder.addImport('q', 'gc', void_sig);
  // First call the gc, then check if the object still exists.
  builder.addFunction('main', ref_sig)
      .addBody([
        kExprCallFunction, gc_index,                    // call gc
        kExprLocalGet, 0, kExprCallFunction, imp_index  // call import
      ])
      .exportFunc();

  function checkFunction(value) {
    assertSame(main, value);
  }

  const main =
      builder.instantiate({q: {func: checkFunction, gc: gc}}).exports.main;

  main(main);
})();

(function testPassAnyFuncWithGCInWrapper() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const kSig_a_iai =
      makeSig([kWasmI32, kWasmAnyFunc, kWasmI32], [kWasmAnyFunc]);
  const sig_index = builder.addType(kSig_a_iai);
  builder.addFunction('main', sig_index)
      .addBody([kExprLocalGet, 1])
      .exportFunc();

  const main = builder.instantiate().exports.main;

  const triggerGCParam = {
    valueOf: () => {
      gc();
      return 17;
    }
  };

  const result = main(triggerGCParam, main, triggerGCParam);
  assertSame(main, result);
})();

(function testAnyFuncDefaultValue() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const sig_index = builder.addType(kSig_a_v);
  builder.addFunction('main', sig_index)
      .addLocals(kWasmAnyFunc, 1)
      .addBody([kExprLocalGet, 0])
      .exportFunc();

  const main = builder.instantiate().exports.main;
  assertEquals(null, main());
})();

(function testAssignNullToAnyFuncLocal() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const sig_index = builder.addType(kSig_a_a);
  builder.addFunction('main', sig_index)
      .addBody([kExprRefNull, kAnyFuncCode, kExprLocalSet, 0, kExprLocalGet, 0])
      .exportFunc();

  const main = builder.instantiate().exports.main;
  assertEquals(null, main(main));
})();

(function testImplicitReturnNullAsAnyFunc() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const sig_index = builder.addType(kSig_a_v);
  builder.addFunction('main', sig_index)
      .addBody([kExprRefNull, kAnyFuncCode])
      .exportFunc();

  const main = builder.instantiate().exports.main;
  assertEquals(null, main());
})();

(function testExplicitReturnNullAsAnyFunc() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const sig_index = builder.addType(kSig_a_v);
  builder.addFunction('main', sig_index)
      .addBody([kExprRefNull, kAnyFuncCode, kExprReturn])
      .exportFunc();

  const main = builder.instantiate().exports.main;
  assertEquals(null, main());
})();

(function testRefFuncOutOfBounds() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_a_v).addBody([kExprRefFunc, 10]);

  assertThrows(() => builder.toModule(), WebAssembly.CompileError);
})();

(function testRefFuncIsCallable() {
  print(arguments.callee.name);
  const expected = 54;
  const builder = new WasmModuleBuilder();
  const function_index = builder.addFunction('hidden', kSig_i_v)
                             .addBody([kExprI32Const, expected])
                             .index;
  builder.addDeclarativeElementSegment([function_index]);
  builder.addFunction('main', kSig_a_v)
      .addBody([kExprRefFunc, function_index])
      .exportFunc();

  const instance = builder.instantiate();
  assertEquals(expected, instance.exports.main()());
})();

(function testRefFuncPreservesIdentity() {
  print(arguments.callee.name);
  const expected = 54;
  const builder = new WasmModuleBuilder();
  const foo = builder.addFunction('foo', kSig_i_v)
                  .addBody([kExprI32Const, expected])
                  .exportFunc();
  builder.addDeclarativeElementSegment([foo.index]);
  builder.addFunction('main', kSig_a_v)
      .addBody([kExprRefFunc, foo.index])
      .exportFunc();

  const instance = builder.instantiate();
  assertSame(instance.exports.foo, instance.exports.main());
})();
