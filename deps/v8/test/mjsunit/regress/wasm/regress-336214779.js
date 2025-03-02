// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --jit-fuzzing --wasm-staging
// Flags: --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");
d8.file.execute("test/mjsunit/wasm/exceptions-utils.js");

(function Regress336214779() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let tag_index = builder.addImportedTag("", "tag", kSig_v_r);

  function throw_ref(x) {
    %ScheduleGCInStackCheck();
    throw x;
  }
  let kJSThrowRef = builder.addImport("", "throw_ref", kSig_r_r);
  let try_sig_index = builder.addType(kSig_r_v);
  builder.addFunction("test", kSig_r_r)
    .addBody([
      kExprTryTable, try_sig_index, 1,
      kCatchNoRef, tag_index, 0,
        kExprLocalGet, 0,
        kExprCallFunction, kJSThrowRef,
      kExprEnd,
    ])
    .exportFunc();

  let tag = new WebAssembly.Tag({parameters: ['externref'], results: []});
  let instance = builder.instantiate({"": {
      throw_ref: throw_ref,
      tag: tag,
  }});

  let obj = new WebAssembly.Exception(tag, [{}]);
  instance.exports.test(obj);
  instance.exports.test(obj);
})();
