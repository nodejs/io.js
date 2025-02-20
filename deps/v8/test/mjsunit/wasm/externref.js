// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function testExternRefIdentityFunction() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_r_r)
      .addBody([kExprLocalGet, 0])
      .exportFunc();

  const instance = builder.instantiate();

  let obj = {'hello' : 'world'};
  assertEquals(obj, instance.exports.main(obj));
  assertEquals(1234, instance.exports.main(1234));
  assertEquals(123.4, instance.exports.main(123.4));
  assertEquals(undefined, instance.exports.main(undefined));
  assertEquals(null, instance.exports.main(null));
  assertEquals(print, instance.exports.main(print));
})();

(function testPassExternRefToImportedFunction() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const sig_index = builder.addType(kSig_v_r);
  const imp_index = builder.addImport("q", "func", sig_index);
  builder.addFunction('main', sig_index)
      .addBody([kExprLocalGet, 0,
      kExprCallFunction, imp_index])
      .exportFunc();

  function checkFunction(value) {
    assertEquals('world', value.hello);
  }

  const instance = builder.instantiate({q: {func: checkFunction}});

  instance.exports.main({hello: 'world'});
})();

(function testPassExternRefWithGCWithLocals() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const ref_sig = builder.addType(kSig_v_r);
  const void_sig = builder.addType(kSig_v_v);
  const imp_index = builder.addImport("q", "func", ref_sig);
  const gc_index = builder.addImport("q", "gc", void_sig);
  // First call the gc, then check if the object still exists.
  builder.addFunction('main', ref_sig)
      .addLocals(kWasmExternRef, 10)
      .addBody([
        kExprLocalGet, 0, kExprLocalSet, 1,             // Set local
        kExprLocalGet, 0, kExprLocalSet, 2,             // Set local
        kExprLocalGet, 0, kExprLocalSet, 3,             // Set local
        kExprLocalGet, 0, kExprLocalSet, 4,             // Set local
        kExprLocalGet, 0, kExprLocalSet, 5,             // Set local
        kExprLocalGet, 0, kExprLocalSet, 6,             // Set local
        kExprLocalGet, 0, kExprLocalSet, 7,             // Set local
        kExprLocalGet, 0, kExprLocalSet, 8,             // Set local
        kExprLocalGet, 0, kExprLocalSet, 9,             // Set local
        kExprLocalGet, 0, kExprLocalSet, 10,            // Set local
        kExprCallFunction, gc_index,                    // call gc
        kExprLocalGet, 9, kExprCallFunction, imp_index  // call import
      ])
      .exportFunc();

  function checkFunction(value) {
    assertEquals('world', value.hello);
  }

  const instance = builder.instantiate({q: {func: checkFunction, gc: gc}});

  instance.exports.main({hello: 'world'});
})();

(function testPassExternRefWithGC() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const ref_sig = builder.addType(kSig_v_r);
  const void_sig = builder.addType(kSig_v_v);
  const imp_index = builder.addImport("q", "func", ref_sig);
  const gc_index = builder.addImport("q", "gc", void_sig);
  // First call the gc, then check if the object still exists.
  builder.addFunction('main', ref_sig)
      .addBody([
        kExprCallFunction, gc_index,                    // call gc
        kExprLocalGet, 0, kExprCallFunction, imp_index  // call import
      ])
      .exportFunc();

  function checkFunction(value) {
    assertEquals('world', value.hello);
  }

  const instance = builder.instantiate({q: {func: checkFunction, gc: gc}});

  instance.exports.main({hello: 'world'});
})();

(function testPassExternRefWithGCWithStackParameters() {
  print(arguments.callee.name);
  const num_params = 15;
  for (let index = 0; index < num_params; index++) {
    const builder = new WasmModuleBuilder();
    // Make a signature with {num_params} many externref parameters.
    const mysig = makeSig(Array(num_params).fill(kWasmExternRef), []);
    const main_sig = builder.addType(mysig);
    const ref_sig = builder.addType(kSig_v_r);
    const void_sig = builder.addType(kSig_v_v);
    const imp_index = builder.addImport('q', 'func', ref_sig);
    const gc_index = builder.addImport('q', 'gc', void_sig);
    // First call the gc, then check if the object still exists.
    builder.addFunction('main', main_sig)
        .addBody([
          kExprCallFunction, gc_index,                        // call gc
          kExprLocalGet, index, kExprCallFunction, imp_index  // call import
        ])
        .exportFunc();

    function checkFunction(value) {
      assertEquals(index, value.hello);
    }

    const instance = builder.instantiate({q: {func: checkFunction, gc: gc}});

    // Pass {num_params} many parameters to main. Note that it is important
    // that no other references to these objects exist. They are kept alive
    // only through references stored in the parameters slots of a stack frame.
    instance.exports.main(
        {hello: 0}, {hello: 1}, {hello: 2}, {hello: 3}, {hello: 4}, {hello: 5},
        {hello: 6}, {hello: 7}, {hello: 8}, {hello: 9}, {hello: 10},
        {hello: 11}, {hello: 12}, {hello: 13}, {hello: 14});
  }
})();

(function testPassExternRefWithGCInWrapper() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const kSig_r_iri = makeSig([kWasmI32, kWasmExternRef, kWasmI32], [kWasmExternRef]);
  const sig_index = builder.addType(kSig_r_iri);
  builder.addFunction('main', sig_index)
      .addBody([kExprLocalGet, 1])
      .exportFunc();

  const instance = builder.instantiate();

  const triggerGCParam = {
    valueOf: () => {
      gc();
      return 17;
    }
  };

  const result = instance.exports.main(triggerGCParam, {hello: 'world'}, triggerGCParam);
  assertEquals('world', result.hello);
})();

(function testExternRefNull() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_r_v)
      .addBody([kExprRefNull, kExternRefCode])
      .exportFunc();

  const instance = builder.instantiate();

  assertEquals(null, instance.exports.main());
})();

(function testExternRefIsNull() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_i_r)
      .addBody([kExprLocalGet, 0, kExprRefIsNull])
      .exportFunc();

  const instance = builder.instantiate();

  assertEquals(0, instance.exports.main({'hello' : 'world'}));
  assertEquals(0, instance.exports.main(1234));
  assertEquals(0, instance.exports.main(0));
  assertEquals(0, instance.exports.main(123.4));
  assertEquals(0, instance.exports.main(undefined));
  assertEquals(1, instance.exports.main(null));
  assertEquals(0, instance.exports.main(print));
})();

(function testExternRefNullIsNull() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_i_v)
      .addBody([kExprRefNull, kExternRefCode, kExprRefIsNull])
      .exportFunc();

  const instance = builder.instantiate();

  assertEquals(1, instance.exports.main());
})();

(function testExternRefLocalDefaultValue() {
  print(arguments.callee.name);
  const numLocals = 3;
  for (let i = 0; i < numLocals; ++i) {
    const builder = new WasmModuleBuilder();
    builder.addFunction('main', kSig_r_v)
        .addBody([kExprLocalGet, i])
        .addLocals(kWasmExternRef, numLocals)
        .exportFunc();

    const instance = builder.instantiate();

    assertEquals(null, instance.exports.main());
  }
})();

(function testImplicitReturnNullAsExternRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const sig_index = builder.addType(kSig_r_v);
  builder.addFunction('main', sig_index)
      .addBody([kExprRefNull, kExternRefCode])
      .exportFunc();

  const main = builder.instantiate().exports.main;
  assertEquals(null, main());
})();

(function testExplicitReturnNullAsExternRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const sig_index = builder.addType(kSig_r_v);
  builder.addFunction('main', sig_index)
      .addBody([kExprRefNull, kExternRefCode, kExprReturn])
      .exportFunc();

  const main = builder.instantiate().exports.main;
  assertEquals(null, main());
})();

(function testGCInStackCheck() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();

  const gc_sig = builder.addType(kSig_v_v);
  const mysig = makeSig(
      [
        kWasmExternRef, kWasmI32, kWasmExternRef, kWasmExternRef, kWasmExternRef
      ],
      []);
  const func_sig = builder.addType(mysig);
  const triggerGC_index = builder.addImport('q', 'triggerGC', gc_sig);
  const func_index = builder.addImport('q', 'func', func_sig);

  const foo = builder.addFunction('foo', func_sig).addBody([
    kExprLocalGet, 0, // --
    kExprLocalGet, 1, // --
    kExprLocalGet, 2, // --
    kExprLocalGet, 3, // --
    kExprLocalGet, 4, // --
    kExprCallFunction, func_index
  ]);

  builder.addFunction('main', func_sig)
      .addBody([
        kExprCallFunction, triggerGC_index,  // --
        kExprLocalGet, 0,                    // --
        kExprLocalGet, 1,                    // --
        kExprLocalGet, 2,                    // --
        kExprLocalGet, 3,                    // --
        kExprLocalGet, 4,                    // --
        kExprCallFunction, foo.index
      ])
      .exportFunc();

  const instance = builder.instantiate({
    q: {
      triggerGC: () => %ScheduleGCInStackCheck(),
      func: (ref) => assertEquals(ref.hello, 4)
    }
  });

  instance.exports.main({hello: 4}, 5, {world: 6}, null, {bar: 7});
})();

(function testGCInStackCheckUnalignedFrameSize() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();

  const gc_sig = builder.addType(kSig_v_v);
  const mysig = makeSig(
      [
        kWasmExternRef, kWasmI32, kWasmExternRef, kWasmExternRef, kWasmExternRef
      ],
      []);
  const func_sig = builder.addType(mysig);
  const triggerGC_index = builder.addImport('q', 'triggerGC', gc_sig);
  const func_index = builder.addImport('q', 'func', func_sig);

  const foo = builder.addFunction('foo', func_sig).addBody([
    kExprLocalGet, 0, // --
    kExprLocalGet, 1, // --
    kExprLocalGet, 2, // --
    kExprLocalGet, 3, // --
    kExprLocalGet, 4, // --
    kExprCallFunction, func_index
  ]).addLocals(kWasmI32, 1);

  builder.addFunction('main', func_sig)
      .addBody([
        kExprCallFunction, triggerGC_index,  // --
        kExprLocalGet, 0,                    // --
        kExprLocalGet, 1,                    // --
        kExprLocalGet, 2,                    // --
        kExprLocalGet, 3,                    // --
        kExprLocalGet, 4,                    // --
        kExprCallFunction, foo.index
      ])
      .exportFunc();

  const instance = builder.instantiate({
    q: {
      triggerGC: () => %ScheduleGCInStackCheck(),
      func: (ref) => assertEquals(ref.hello, 4)
    }
  });

  instance.exports.main({hello: 4}, 5, {world: 6}, null, {bar: 7});
})();

(function MultiReturnRefTest() {
  print("MultiReturnTest");
  let builder = new WasmModuleBuilder();
  let gc_sig = builder.addType(kSig_v_v);
  let sig = makeSig([kWasmExternRef],
      [kWasmExternRef, kWasmExternRef, kWasmExternRef, kWasmExternRef]);

  let gc_index = builder.addImport('q', 'gc', gc_sig);
  let callee = builder.addFunction("callee", sig)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 0,
      kExprLocalGet, 0,
      kExprLocalGet, 0,
    ]);
  builder.addFunction("main", sig)
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, callee.index,
      kExprCallFunction, gc_index,
    ])
    .exportAs("main");

  let instance = builder.instantiate({
    q: { gc: () => gc() }
  });
  assertEquals(instance.exports.main(null), [null, null, null, null]);
})();
