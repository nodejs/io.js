// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function CheckThrowOnBadSuspension() {
    print(arguments.callee.name);
    let builder = new WasmModuleBuilder();
    let js_tag = builder.addImportedTag("", "tag", kSig_v_r);
    try_sig_index = builder.addType(kSig_i_v);

    let promise42 = new WebAssembly.Suspending(() => Promise.resolve(42));
    let kPromise42Ref = builder.addImport("", "promise42", kSig_i_v);

    builder.addFunction("test", kSig_i_v)
      .addBody([
        kExprTry, try_sig_index,
          kExprCallFunction, kPromise42Ref,
          kExprReturn,  // If there was no trap or exception, return
        kExprCatch, js_tag,
          kExprI32Const, 43,
          kExprReturn,
        kExprEnd,
      ])
      .exportFunc();

    let instance = builder.instantiate({"": {
        promise42: promise42,
        tag: WebAssembly.JSTag,
    }});

    assertEquals(43, instance.exports.test());
  })();

(function CheckThrowOnReentrance() {
    print(arguments.callee.name);
    let builder = new WasmModuleBuilder();
    let js_tag = builder.addImportedTag("", "tag", kSig_v_r);
    try_sig_index = builder.addType(kSig_i_v);

    let promise42 = new WebAssembly.Suspending(() => Promise.resolve(42));
    let kPromise42Ref = builder.addImport("", "promise42", kSig_i_v);
    let backChnnlRef = builder.addImport("","backChnnl",kSig_i_v);

    builder.addFunction("main", kSig_i_v)
      .addBody([
        kExprTry, try_sig_index,
          kExprCallFunction, backChnnlRef,
          kExprReturn,  // If there was no trap or exception, return
        kExprCatch, js_tag,
          kExprI32Const, 43,
          kExprReturn,
        kExprEnd,
      ])
      .exportFunc();

      builder.addFunction("inner", kSig_i_v)
      .addBody([
        kExprCallFunction, kPromise42Ref,
      ])
      .exportFunc();

    let backChnnl = ()=>instance.exports.inner();
    let instance = builder.instantiate({"": {
        promise42: promise42,
        backChnnl: backChnnl,
        tag: WebAssembly.JSTag,
    }});

    wrapped_export = WebAssembly.promising(instance.exports.main);

    assertPromiseResult(wrapped_export(), v => assertEquals(43, v));
  })();

(function TestUnwrappedExportError() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  import_index = builder.addImport('m', 'import', kSig_v_v);
  builder.addFunction("test", kSig_v_v)
      .addBody([
          kExprCallFunction, import_index,
      ]).exportFunc();
  let js_import = new WebAssembly.Suspending(() => Promise.resolve());
  let instance = builder.instantiate({m: {import: js_import}});
  assertThrows(instance.exports.test, WebAssembly.SuspendError);
})();
