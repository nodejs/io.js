// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestPostModule() {
  let builder = new WasmModuleBuilder();
  builder.addFunction("add", kSig_i_ii)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();

  let module = builder.toModule();

  let workerScript = `
    onmessage = function({data:module}) {
      try {
        let instance = new WebAssembly.Instance(module);
        let result = instance.exports.add(40, 2);
        postMessage(result);
      } catch(e) {
        postMessage('ERROR: ' + e);
      }
    }
  `;

  let realm = Realm.create();
  Realm.shared = { m:module, s:workerScript };

  let realmScript = `
    let worker = new Worker(Realm.shared.s, {type: 'string'});
    worker.postMessage(Realm.shared.m);
    let message = worker.getMessage();
    worker.terminate();
    message;
  `;
  let message = Realm.eval(realm, realmScript);
  assertEquals(42, message);
})();
