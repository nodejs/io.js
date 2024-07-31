// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --experimental-wasm-jspi
// Flags: --expose-gc --wasm-stack-switching-stack-size=100

// We pick a small stack size to run the stack overflow test quickly, but big
// enough to run all the tests.

load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestSuspender() {
  print(arguments.callee.name);
  let suspender = new WebAssembly.Suspender();
  assertTrue(suspender.toString() == "[object WebAssembly.Suspender]");
  assertThrows(() => WebAssembly.Suspender(), TypeError,
      /WebAssembly.Suspender must be invoked with 'new'/);
})();

(function TestSuspenderTypes() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_i_ri = makeSig([kWasmExternRef, kWasmI32], [kWasmI32]);
  let sig_ii_r = makeSig([kWasmExternRef], [kWasmI32, kWasmI32]);
  let sig_v_ri = makeSig([kWasmExternRef, kWasmI32], []);
  builder.addImport('m', 'import', sig_v_ri);
  builder.addFunction("export", sig_i_ri)
      .addBody([kExprLocalGet, 1]).exportFunc();
  builder.addFunction("wrong1", sig_ii_r)
      .addBody([kExprI32Const, 0, kExprI32Const, 0]).exportFunc();
  builder.addFunction("wrong2", kSig_v_r)
      .addBody([]).exportFunc();
  builder.addFunction("wrong3", kSig_i_v)
      .addBody([kExprI32Const, 0]).exportFunc();
  let suspender = new WebAssembly.Suspender();
  function js_import(i) {
    return Promise.resolve(42);
  }

  // Wrap the import, instantiate the module, and wrap the export.
  let import_wrapper = new WebAssembly.Function(
      {parameters: ['externref', 'i32'], results: []},
      js_import,
      {suspending: 'first'});
  let instance = builder.instantiate({'m': {'import': import_wrapper}});
  let export_wrapper = ToPromising(instance.exports.export);

  // Check type errors.
  assertThrows(() => new WebAssembly.Function(
      {parameters: ['externref'], results: ['externref']},
      js_import,
      {suspending: 'foo'}),
      TypeError,
      /JS Promise Integration: Expected suspender position to be "first", "last" or "none"/);
  // Bad inner signature (promising)
  for (const f of [instance.exports.wrong1, instance.exports.wrong2, instance.exports.wrong3]) {
    assertThrows(() => new WebAssembly.Function(
        {parameters: ['i32'], results: ['externref']},
        f,
        {promising: 'first'}),
        TypeError,
        /Incompatible signature for promising function/);
  }
  // Signature mismatch (suspending)
  assertThrows(() => new WebAssembly.Function(
      {parameters: ['externref'], results: []},
      new WebAssembly.Function(
          {parameters: [], results: ['i32']}, js_import),
      {suspending: 'first'}),
      TypeError,
      /Incompatible signature for suspending function/);
  // Signature mismatch (promising)
  assertThrows(() => new WebAssembly.Function(
      {parameters: ['externref', 'i32'], results: ['i32']},
      instance.exports.export,
      {promising: 'first'}),
      TypeError,
      /Incompatible signature for promising function/);

  // Check the wrapped export's signature.
  let export_sig = export_wrapper.type();
  assertEquals(['i32'], export_sig.parameters);
  assertEquals(['externref'], export_sig.results);

  // Check the wrapped import's signature.
  let import_sig = import_wrapper.type();
  assertEquals(['externref', 'i32'], import_sig.parameters);
  assertEquals([], import_sig.results);
})();

(function TestStackSwitchSuspenderType() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("test", kSig_i_r)
      .addBody([kExprI32Const, 0]).exportFunc();
  let instance = builder.instantiate();
  let suspender = new WebAssembly.Suspender();
  let wrapper = ToPromising(instance.exports.test);
})();

(function TestStackSwitchNoSuspend() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true, false).exportAs('g');
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprI32Const, 42,
          kExprGlobalSet, 0,
          kExprI32Const, 0]).exportFunc();
  let instance = builder.instantiate();
  let wrapper = ToPromising(instance.exports.test);
  wrapper();
  assertEquals(42, instance.exports.g.value);
})();

(function TestStackSwitchSuspend() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  import_index = builder.addImport('m', 'import', kSig_i_r);
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, import_index, // suspend
      ]).exportFunc();
  let js_import = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      () => Promise.resolve(42),
      {suspending: 'first'});
  let instance = builder.instantiate({m: {import: js_import}});
  let wrapped_export = ToPromising(instance.exports.test);
  let combined_promise = wrapped_export();
  assertPromiseResult(combined_promise, v => assertEquals(42, v));

  // Also try with a JS function with a mismatching arity.
  js_import = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      (unused) => Promise.resolve(42),
      {suspending: 'first'});
  instance = builder.instantiate({m: {import: js_import}});
  wrapped_export = ToPromising(instance.exports.test);
  combined_promise = wrapped_export();
  assertPromiseResult(combined_promise, v => assertEquals(42, v));

  // Also try with a proxy.
  js_import = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      new Proxy(() => Promise.resolve(42), {}),
      {suspending: "first"});
  instance = builder.instantiate({m: {import: js_import}});
  wrapped_export = ToPromising(instance.exports.test);
  combined_promise = wrapped_export();
  assertPromiseResult(combined_promise, v => assertEquals(42, v));
  %CheckIsOnCentralStack();
})();

// Check that we can suspend back out of a resumed computation.
(function TestStackSwitchSuspendLoop() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true, false).exportAs('g');
  import_index = builder.addImport('m', 'import', kSig_i_r);
  // Pseudo-code for the wasm function:
  // for (i = 0; i < 5; ++i) {
  //   g = g + import();
  // }
  builder.addFunction("test", kSig_i_r)
      .addLocals(kWasmI32, 1)
      .addBody([
          kExprI32Const, 5,
          kExprLocalSet, 1,
          kExprLoop, kWasmVoid,
            kExprLocalGet, 0,
            kExprCallFunction, import_index, // suspend
            kExprGlobalGet, 0, // resume
            kExprI32Add,
            kExprGlobalSet, 0,
            kExprLocalGet, 1,
            kExprI32Const, 1,
            kExprI32Sub,
            kExprLocalTee, 1,
            kExprBrIf, 0,
          kExprEnd,
          kExprI32Const, 0,
      ]).exportFunc();
  let i = 0;
  // The n-th call to the import returns a promise that resolves to n.
  function js_import() {
    return Promise.resolve(++i);
  };
  let wasm_js_import = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      js_import,
      {suspending: 'first'});
  let instance = builder.instantiate({m: {import: wasm_js_import}});
  let wrapped_export = ToPromising(instance.exports.test);
  let chained_promise = wrapped_export();
  assertEquals(0, instance.exports.g.value);
  assertPromiseResult(chained_promise, _ => assertEquals(15, instance.exports.g.value));
})();

// Call the GC in the import call.
(function TestStackSwitchGC() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let gc_index = builder.addImport('m', 'gc', kSig_v_r);
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, gc_index,
          kExprI32Const, 0
      ]).exportFunc();
  let js_import = new WebAssembly.Function(
          {parameters: ['externref'], results: []},
          gc,
          {suspending: 'first'});
  let instance = builder.instantiate({'m': {'gc': js_import}});
  let wrapper = ToPromising(instance.exports.test);
  wrapper();
})();

// Call the GC during param conversion.
(function TestStackSwitchGC2() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmI32], [kWasmI32]);
  let import_index = builder.addImport('m', 'import', sig);
  builder.addFunction("test", sig)
      .addBody([
          kExprLocalGet, 0,
          kExprLocalGet, 1,
          kExprCallFunction, import_index,
      ]).exportFunc();
  let js_import = new WebAssembly.Function(
          {parameters: ['externref', 'i32'], results: ['i32']},
          (v) => { return Promise.resolve(v) },
          {suspending: 'first'});
  let instance = builder.instantiate({'m': {'import': js_import}});
  let wrapper = ToPromising(instance.exports.test);
  let arg = { valueOf: () => { gc(); return 24; } };
  assertPromiseResult(wrapper(arg), v => assertEquals(arg.valueOf(), v));
})();

// Check that the suspender does not suspend if the import's
// return value is not a promise.
(function TestStackSwitchNoPromise() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true, false).exportAs('g');
  import_index = builder.addImport('m', 'import', kSig_i_r);
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, import_index, // suspend
          kExprGlobalSet, 0, // resume
          kExprGlobalGet, 0,
      ]).exportFunc();
  function js_import() {
    return 42
  };
  let wasm_js_import = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      js_import,
      {suspending: 'first'});
  let instance = builder.instantiate({m: {import: wasm_js_import}});
  let wrapped_export = ToPromising(instance.exports.test);
  let result = wrapped_export();
  assertEquals(42, instance.exports.g.value);
})();

(function TestStackSwitchSuspendArgs() {
  print(arguments.callee.name);
  function reduce(array) {
    // a[0] + a[1] * 2 + a[2] * 3 + ...
    return array.reduce((prev, cur, i) => prev + cur * (i + 1));
  }
  let builder = new WasmModuleBuilder();
  // Number of param registers + 1 for both types.
  let sig = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
      kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32], [kWasmI32]);
  import_index = builder.addImport('m', 'import', sig);
  builder.addFunction("test", sig)
      .addBody([
          kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kExprLocalGet, 3,
          kExprLocalGet, 4, kExprLocalGet, 5, kExprLocalGet, 6, kExprLocalGet, 7,
          kExprLocalGet, 8, kExprLocalGet, 9, kExprLocalGet, 10, kExprLocalGet, 11,
          kExprLocalGet, 12, kExprLocalGet, 13,
          kExprCallFunction, import_index, // suspend
      ]).exportFunc();
  let suspender = new WebAssembly.Suspender();
  function js_import(i1, i2, i3, i4, i5, i6, f1, f2, f3, f4, f5, f6, f7) {
    return Promise.resolve(reduce(Array.from(arguments)));
  };
  let wasm_js_import = new WebAssembly.Function(
      {parameters: ['externref', 'i32', 'i32', 'i32', 'i32', 'i32', 'i32', 'f32',
        'f32', 'f32', 'f32', 'f32', 'f32', 'f32'], results: ['i32']},
        js_import,
        {suspending: 'first'});

  let instance = builder.instantiate({m: {import: wasm_js_import}});
  let wrapped_export = ToPromising(instance.exports.test);
  let args = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13];
  let combined_promise =
      wrapped_export.apply(null, args);
  assertPromiseResult(combined_promise, v => assertEquals(reduce(args), v));
})();

(function TestStackSwitchReturnFloat() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef], [kWasmF32]);
  import_index = builder.addImport('m', 'import', sig);
  builder.addFunction("test", sig)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, import_index, // suspend
      ]).exportFunc();
  function js_import() {
    return Promise.resolve(0.5);
  };
  let wasm_js_import = new WebAssembly.Function(
      {parameters: ['externref'], results: ['f32']},
      js_import,
      {suspending: 'first'});

  let instance = builder.instantiate({m: {import: wasm_js_import}});
  let wrapped_export = ToPromising(instance.exports.test);
  let combined_promise = wrapped_export();
  assertPromiseResult(combined_promise, v => assertEquals(0.5, v));
})();

// Throw an exception before suspending. The export wrapper should return a
// promise rejected with the exception.
(function TestStackSwitchException1() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let tag = builder.addTag(kSig_v_v);
  builder.addFunction("throw", kSig_i_r)
      .addBody([kExprThrow, tag]).exportFunc();
  let instance = builder.instantiate();
  let wrapper = ToPromising(instance.exports.throw);
  assertThrowsAsync(wrapper(), WebAssembly.Exception);
})();

// Throw an exception after the first resume event, which propagates to the
// promise wrapper.
(function TestStackSwitchException2() {
  print(arguments.callee.name);
  let tag = new WebAssembly.Tag({parameters: []});
  let builder = new WasmModuleBuilder();
  import_index = builder.addImport('m', 'import', kSig_i_r);
  tag_index = builder.addImportedTag('m', 'tag', kSig_v_v);
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, import_index,
          kExprThrow, tag_index
      ]).exportFunc();
  function js_import() {
    return Promise.resolve(42);
  };
  let wasm_js_import = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      js_import,
      {suspending: 'first'});

  let instance = builder.instantiate({m: {import: wasm_js_import, tag: tag}});
  let wrapped_export = ToPromising(instance.exports.test);
  let combined_promise = wrapped_export();
  assertThrowsAsync(combined_promise, WebAssembly.Exception);
})();

(function TestStackSwitchPromiseReject() {
  print(arguments.callee.name);
  let tag = new WebAssembly.Tag({parameters: ['i32']});
  let builder = new WasmModuleBuilder();
  import_index = builder.addImport('m', 'import', kSig_i_r);
  tag_index = builder.addImportedTag('m', 'tag', kSig_v_i);
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprTry, kWasmI32,
          kExprLocalGet, 0,
          kExprCallFunction, import_index,
          kExprCatch, tag_index,
          kExprEnd,
      ]).exportFunc();
  function js_import() {
    return Promise.reject(new WebAssembly.Exception(tag, [42]));
  };
  let wasm_js_import = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      js_import,
      {suspending: 'first'});

  let instance = builder.instantiate({m: {import: wasm_js_import, tag: tag}});
  let wrapped_export = ToPromising(instance.exports.test);
  let combined_promise = wrapped_export();
  assertPromiseResult(combined_promise, v => assertEquals(v, 42));
})();

function TestNestedSuspenders(suspend) {
  // Nest two suspenders. The call chain looks like:
  // outer (wasm) -> outer (js) -> inner (wasm) -> inner (js)
  // If 'suspend' is true, the inner JS function returns a Promise, which
  // suspends the inner wasm function, which returns a Promise, which suspends
  // the outer wasm function, which returns a Promise. The inner Promise
  // resolves first, which resumes the inner continuation. Then the outer
  // promise resolves which resumes the outer continuation.
  // If 'suspend' is false, the inner and outer JS functions return a regular
  // value and no computation is suspended.
  let builder = new WasmModuleBuilder();
  inner_index = builder.addImport('m', 'inner', kSig_i_r);
  outer_index = builder.addImport('m', 'outer', kSig_i_r);
  builder.addFunction("outer", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, outer_index
      ]).exportFunc();
  builder.addFunction("inner", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, inner_index
      ]).exportFunc();

  let inner = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      () => suspend ? Promise.resolve(42) : 42,
      {suspending: 'first'});

  let export_inner;
  let outer = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      () => suspend ? export_inner() : 42,
      {suspending: 'first'});

  let instance = builder.instantiate({m: {inner, outer}});
  export_inner = ToPromising(instance.exports.inner);
  let export_outer = ToPromising(instance.exports.outer);
  assertPromiseResult(export_outer(), v => assertEquals(42, v));
}

(function TestNestedSuspendersSuspend() {
  print(arguments.callee.name);
  TestNestedSuspenders(true);
})();

(function TestNestedSuspendersNoSuspend() {
  print(arguments.callee.name);
  TestNestedSuspenders(false);
})();

(function Regress13231() {
  print(arguments.callee.name);
  // Check that a promising function with no return is allowed.
  let builder = new WasmModuleBuilder();
  let sig_v_r = makeSig([kWasmExternRef], []);
  builder.addFunction("export", sig_v_r).addBody([]).exportFunc();
  let instance = builder.instantiate();
  let export_wrapper = ToPromising(instance.exports.export);
  let export_sig = export_wrapper.type();
  assertEquals([], export_sig.parameters);
  assertEquals(['externref'], export_sig.results);
})();

(function TestStackOverflow() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, 0
          ]).exportFunc();
  let instance = builder.instantiate();
  let wrapper = ToPromising(instance.exports.test);
  assertThrowsAsync(wrapper(), RangeError, /Maximum call stack size exceeded/);
})();

(function TestBadSuspender() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let import_index = builder.addImport('m', 'import', kSig_i_r);
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, import_index, // suspend
      ]).exportFunc();
  builder.addFunction("return_suspender", kSig_r_r)
      .addBody([
          kExprLocalGet, 0
      ]).exportFunc();
  let js_import = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      () => Promise.resolve(42),
      {suspending: 'first'});
  let instance = builder.instantiate({m: {import: js_import}});
  let suspender = ToPromising(instance.exports.return_suspender)();
  for (s of [suspender, null, undefined, {}]) {
    assertThrows(() => instance.exports.test(s),
        WebAssembly.RuntimeError,
        /invalid suspender object for suspend/);
  }
})();

(function SuspendCallRef() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let funcref_type = builder.addType(kSig_i_r);
  let table = builder.addTable(wasmRefNullType(funcref_type), 1)
                         .exportAs('table');
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprI32Const, 0, kExprTableGet, table.index,
          kExprCallRef, funcref_type,
      ]).exportFunc();
  let instance = builder.instantiate();

  let funcref = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      () => Promise.resolve(42),
      {suspending: 'first'});
  instance.exports.table.set(0, funcref);

  let exp = new WebAssembly.Function(
      {parameters: [], results: ['externref']},
      instance.exports.test,
      {promising: 'first'});
  assertPromiseResult(exp(), v => assertEquals(42, v));
})();

(function SuspendCallIndirect() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let functype = builder.addType(kSig_i_r);
  let table = builder.addTable(kWasmFuncRef, 10, 10);
  let callee = builder.addImport('m', 'f', kSig_i_r);
  builder.addActiveElementSegment(table, wasmI32Const(0), [callee]);
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprI32Const, 0,
          kExprCallIndirect, functype, table.index,
      ]).exportFunc();

  let create_promise = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      () => Promise.resolve(42),
      {suspending: 'first'});

  let instance = builder.instantiate({m: {f: create_promise}});

  let exp = new WebAssembly.Function(
      {parameters: [], results: ['externref']},
      instance.exports.test,
      {promising: 'first'});
  assertPromiseResult(exp(), v => assertEquals(42, v));
})();

(function TestSuspendJSFramesTraps() {
  // The call stack of this test looks like:
  // export1 -> import1 -> export2 -> import2
  // Where export1 is "promising" and import2 is "suspending". Returning a
  // promise from import2 should trap because of the JS import in the middle.
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let import1_index = builder.addImport("m", "import1", kSig_i_v);
  let import2_index = builder.addImport("m", "import2", kSig_i_r);
  builder.addGlobal(kWasmExternRef, true, false);
  builder.addFunction("export1", kSig_i_r)
      .addBody([
          // export1 -> import1 (unwrapped)
          kExprLocalGet, 0,
          kExprGlobalSet, 0,
          kExprCallFunction, import1_index,
      ]).exportFunc();
  builder.addFunction("export2", kSig_i_v)
      .addBody([
          // export2 -> import2 (suspending)
          kExprGlobalGet, 0,
          kExprCallFunction, import2_index,
      ]).exportFunc();
  let instance;
  function import1() {
    // import1 -> export2 (unwrapped)
    instance.exports.export2();
  }
  function import2() {
    return Promise.resolve(0);
  }
  import2 = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      import2,
      {suspending: 'first'});
  instance = builder.instantiate(
      {'m':
        {'import1': import1,
         'import2': import2
        }});
  // export1 (promising)
  let wrapper = new WebAssembly.Function(
      {parameters: [], results: ['externref']},
      instance.exports.export1,
      {promising: 'first'});
  assertThrowsAsync(wrapper(), WebAssembly.RuntimeError,
      /trying to suspend JS frames/);
})();

// Regression test for v8:14094.
// Pass an invalid (null) suspender to the suspending wrapper, but return a
// non-promise. The import should not trap.
(function TestImportCheckOrder() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  import_index = builder.addImport('m', 'import', kSig_i_r);
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, import_index, // suspend
      ]).exportFunc();
  let js_import = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      () => 42,
      {suspending: 'first'});
  let instance = builder.instantiate({m: {import: js_import}});
  assertEquals(42, instance.exports.test(null));
})();

(function TestSwitchingToTheCentralStackForRuntime() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let table = builder.addTable(kWasmExternRef, 1);
  let array_index = builder.addArray(kWasmI32, true);
  let new_space_full_index = builder.addImport('m', 'new_space_full', kSig_v_v);
  builder.addFunction("test", kSig_i_r)
      .addBody([
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kNumericPrefix, kExprTableGrow, table.index]).exportFunc();
  builder.addFunction("test2", kSig_i_r)
      .addBody([
        kExprI32Const, 1]).exportFunc();
  let sig_l_r = makeSig([kWasmExternRef], [kWasmI64]);
  builder.addFunction("test3", sig_l_r)
      .addBody([
        kExprCallFunction, new_space_full_index,
        ...wasmI64Const(0)
        ]).exportFunc();
  builder.addFunction("test4", kSig_v_r)
      .addBody([
        kExprCallFunction, new_space_full_index,
        kExprI32Const, 1,
        kGCPrefix, kExprArrayNewDefault, array_index,
        kExprDrop]).exportFunc();
  function new_space_full() {
    %SimulateNewspaceFull();
  }
  let instance = builder.instantiate({m: {new_space_full}});
  let wrapper = ToPromising(instance.exports.test);
  let wrapper2 = ToPromising(instance.exports.test2);
  let wrapper3 = ToPromising(instance.exports.test3);
  let wrapper4 = ToPromising(instance.exports.test4);
  function switchesToCS(fn) {
    const beforeCall = %WasmSwitchToTheCentralStackCount();
    fn();
    return %WasmSwitchToTheCentralStackCount() - beforeCall;
  }

  // Calling exported functions from the central stack.
  assertEquals(0, switchesToCS(() => instance.exports.test({})));
  assertEquals(0, switchesToCS(() => instance.exports.test2({})));
  assertEquals(0, switchesToCS(() => instance.exports.test3({})));
  assertEquals(0, switchesToCS(() => instance.exports.test4({})));

  // Runtime call to table.grow.
  switchesToCS(wrapper);
  // No runtime calls.
  switchesToCS(wrapper2);
  // Runtime call to allocate the bigint.
  switchesToCS(wrapper3);
  // Runtime call for array.new.
  switchesToCS(wrapper4);
  %CheckIsOnCentralStack();
})();

(function TestSwitchingToTheCentralStackForJS() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  import_index = builder.addImport('m', 'import', kSig_i_r);
  builder.addFunction("test", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, import_index,
      ]).exportFunc();
  let js_import = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      () => {
        %CheckIsOnCentralStack();
        return 123;
      },
      {suspending: 'first'});
  let instance = builder.instantiate({m: {import: js_import}});
  let wrapped_export = ToPromising(instance.exports.test);
  assertPromiseResult(wrapped_export(), v => assertEquals(123, v));
})();

// Test that the wasm-to-js stack params get scanned.
(function TestSwitchingToTheCentralStackManyParams() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  const num_params = 10;
  let params = Array(num_params + 1 /* suspender */).fill(kWasmExternRef);
  const sig = makeSig(params, [kWasmExternRef]);
  const import_index = builder.addImport('m', 'import_', sig);
  let body = [];
  for (let i = 0; i < num_params + 1; ++i) {
    body.push(kExprLocalGet, i);
  }
  body.push(kExprCallFunction, import_index);
  builder.addFunction("test", sig)
      .addBody(body).exportFunc();
  function import_js(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9) {
    gc();
    return [arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9];
  };
  import_js();
  let import_ = new WebAssembly.Function(
      {parameters: Array(num_params + 1).fill('externref'), results: ['externref']},
      import_js,
      {suspending: 'first'});
  let instance = builder.instantiate({m: {import_}});
  let wrapper = ToPromising(instance.exports.test);
  let args = Array(num_params).fill({});
  assertPromiseResult(wrapper(...args), results => { assertEquals(args, results); });
})();

// Similar to TestNestedSuspenders, but trigger an infinite recursion inside the
// outer wasm function after the import call. This is likely to crash if the
// stack limit is not properly restored when we return from the central stack.
// In particular in the nested case, we should preserve and restore the limit of
// each intermediate secondary stack.
(function TestCentralStackReentrency() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  inner_index = builder.addImport('m', 'inner', kSig_i_r);
  outer_index = builder.addImport('m', 'outer', kSig_i_r);
  let stack_overflow = builder.addFunction('stack_overflow', kSig_v_v)
      .addBody([kExprCallFunction, 2]);
  builder.addFunction("outer", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, outer_index,
          kExprCallFunction, stack_overflow.index,
      ]).exportFunc();
  builder.addFunction("inner", kSig_i_r)
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, inner_index
      ]).exportFunc();

  let inner = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      () => Promise.resolve(42),
      {suspending: 'first'});

  let export_inner;
  let outer = new WebAssembly.Function(
      {parameters: ['externref'], results: ['i32']},
      () => export_inner(),
      {suspending: 'first'});

  let instance = builder.instantiate({m: {inner, outer}});
  export_inner = ToPromising(instance.exports.inner);
  let export_outer = ToPromising(instance.exports.outer);
  assertThrowsAsync(export_outer(), RangeError,
      /Maximum call stack size exceeded/);
})();

(function Regress326106962() {
  print(arguments.callee.name);
  const suspender = new WebAssembly.Suspender();
  suspender.foo = "bar";
  gc();
})();
