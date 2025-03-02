// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --wasm-staging

// This file is for the most parts a direct port of
// test/mjsunit/wasm/exceptions-api.js using the new exception handling
// proposal.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");
d8.file.execute("test/mjsunit/wasm/exceptions-utils.js");

(function TestCatchJSTag() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let js_tag = builder.addImportedTag("", "tag", kSig_v_r);

  // Throw a JS object and check that we can catch it and unpack it using
  // WebAssembly.JSTag in try_table.
  function throw_ref(x) {
    throw x;
  }
  let kJSThrowRef = builder.addImport("", "throw_ref", kSig_r_r);
  let try_sig_index = builder.addType(kSig_r_v);
  builder.addFunction("test", kSig_r_r)
    .addBody([
      kExprBlock, try_sig_index,
        kExprTryTable, try_sig_index, 1,
          kCatchNoRef, js_tag, 0,
          kExprLocalGet, 0,
          kExprCallFunction, kJSThrowRef,
        kExprEnd,
      kExprEnd,
    ])
    .exportFunc();

  let instance = builder.instantiate({"": {
      throw_ref: throw_ref,
      tag: WebAssembly.JSTag,
  }});

  let obj = {};
  // Creating a WA.Exception with the JSTag explicitly is not allowed.
  assertThrows(() => new WebAssembly.Exception(WebAssembly.JSTag, [obj]), TypeError);

  // Catch with implicit wrapping.
  assertSame(obj, instance.exports.test(obj));
  // Don't catch with explicit wrapping.
  let not_js_tag = new WebAssembly.Tag({parameters:['externref']});
  let exn = new WebAssembly.Exception(not_js_tag, [obj]);
  assertThrowsEquals(() => instance.exports.test(exn), exn);


  // There is a separate code path for tags with externref type, so also check
  // that everything still works when the tag is *not* the JSTag.

  instance = builder.instantiate({"": {
      throw_ref: throw_ref,
      tag: not_js_tag
  }});

  // Catch with explicit wrapping.
  assertSame(obj, instance.exports.test(new WebAssembly.Exception(not_js_tag, [obj])));
  // Don't catch with explicit wrapping.
  // Don't catch with implicit wrapping.
  assertThrowsEquals(() => instance.exports.test(obj), obj);
})();

(function TestCatchRefJSTag() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let js_tag = builder.addImportedTag("", "tag", kSig_v_r);

  // Throw a JS object and check that we can catch it and unpack it using
  // WebAssembly.JSTag in try_table.
  function throw_ref(x) {
    throw x;
  }
  let kJSThrowRef = builder.addImport("", "throw_ref", kSig_r_r);
  let try_sig_index = builder.addType(kSig_r_v);
  let catch_sig_index = builder.addType(makeSig([], [kWasmExternRef, kWasmExnRef]));
  builder.addFunction("test", kSig_r_r)
    .addBody([
      kExprBlock, catch_sig_index,
        kExprTryTable, try_sig_index, 1,
          kCatchRef, js_tag, 0,
          kExprLocalGet, 0,
          kExprCallFunction, kJSThrowRef,
        kExprEnd,
        kExprReturn,
      kExprEnd,
      kExprDrop,
    ])
    .exportFunc();

  let instance = builder.instantiate({"": {
      throw_ref: throw_ref,
      tag: WebAssembly.JSTag,
  }});

  let obj = {};

  // Catch with implicit wrapping.
  assertSame(obj, instance.exports.test(obj));
  // Don't catch with explicit wrapping.
  let not_js_tag = new WebAssembly.Tag({parameters:['externref']});
  let exn = new WebAssembly.Exception(not_js_tag, [obj]);
  assertThrowsEquals(() => instance.exports.test(exn), exn);


  // There is a separate code path for tags with externref type, so also check
  // that everything still works when the tag is *not* the JSTag.

  instance = builder.instantiate({"": {
      throw_ref: throw_ref,
      tag: not_js_tag
  }});

  // Catch with explicit wrapping.
  assertSame(obj, instance.exports.test(new WebAssembly.Exception(not_js_tag, [obj])));
  // Don't catch with implicit wrapping.
  assertThrowsEquals(() => instance.exports.test(obj), obj);
})();

(function TestCatchRefThrowRefJSTag() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let js_tag = builder.addImportedTag("", "tag", kSig_v_r);

  // Throw a JS object and check that we can catch it and unpack it using
  // WebAssembly.JSTag in try_table, then rethrow it as a JS object with
  // throw_ref.
  function throw_ref(x) {
    throw x;
  }
  let kJSThrowRef = builder.addImport("", "throw_ref", kSig_r_r);
  let try_sig_index = builder.addType(kSig_r_v);
  let catch_sig_index = builder.addType(makeSig([], [kWasmExternRef, kWasmExnRef]));
  builder.addFunction("test", kSig_r_r)
    .addBody([
      kExprBlock, catch_sig_index,
        kExprTryTable, try_sig_index, 1,
          kCatchRef, js_tag, 0,
          kExprLocalGet, 0,
          kExprCallFunction, kJSThrowRef,
        kExprEnd,
        kExprReturn,
      kExprEnd,
      kExprThrowRef,
    ])
    .exportFunc();

  let instance = builder.instantiate({"": {
      throw_ref: throw_ref,
      tag: WebAssembly.JSTag,
  }});

  let obj = {};

  // Catch and rethrown with implicit wrapping.
  assertThrowsEquals(() => instance.exports.test(obj), obj);
  // Don't catch with explicit wrapping.
  let not_js_tag = new WebAssembly.Tag({parameters:['externref']});
  exn = new WebAssembly.Exception(not_js_tag, [obj]);
  assertThrowsEquals(() => instance.exports.test(exn), exn);


  // There is a separate code path for tags with externref type, so also check
  // that everything still works when the tag is *not* the JSTag.

  instance = builder.instantiate({"": {
      throw_ref: throw_ref,
      tag: not_js_tag
  }});

  // Catch and rethrow with explicit wrapping -> not unwrapped in this case.
  exn = new WebAssembly.Exception(not_js_tag, [obj]);
  assertThrowsEquals(() => instance.exports.test(exn), exn);
  // Don't catch with implicit wrapping.
  assertThrowsEquals(() => instance.exports.test(obj), obj);
})();

(function TestThrowJSTag() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let js_tag = builder.addImportedTag("", "tag", kSig_v_r);

  // Throw a JS object with WebAssembly.JSTag and check that we can catch
  // it as-is from JavaScript.
  builder.addFunction("test", kSig_v_r)
    .addBody([
      kExprLocalGet, 0,
      kExprThrow, js_tag,
    ])
    .exportFunc();

  let instance = builder.instantiate({"": {
      tag: WebAssembly.JSTag,
  }});

  let obj = {};
  assertThrowsEquals(() => instance.exports.test(obj), obj);
  assertThrowsEquals(() => instance.exports.test(5), 5);
})();
