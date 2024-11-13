// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function workerCode() {
  function WorkerOnProfileEnd(profile) {
    postMessage(profile.indexOf('foo'));
  }

  onmessage = ({data:wasm_module}) => {
    WebAssembly.instantiate(wasm_module, {q: {func: d8.profiler.triggerSample}})
        .then(instance => {
          instance.exports.foo();
          console.profileEnd();
        });
  };

  d8.profiler.setOnProfileEndListener(WorkerOnProfileEnd);
  // Code logging happens for all code objects when profiling gets started,
  // and when new code objects appear after profiling has started. We want to
  // test the second scenario here. As new code objects appear as the
  // parameter of {OnMessage}, we have to start profiling already here before
  // {onMessage} is called.
  console.profile();
  postMessage('Starting worker');
}

const worker = new Worker(workerCode, {type: 'function'});

assertEquals("Starting worker", worker.getMessage());

const builder = new WasmModuleBuilder();
const sig_index = builder.addType(kSig_v_v);
const imp_index = builder.addImport("q", "func", sig_index);
builder.addFunction('foo', kSig_v_v)
    .addBody([
      kExprCallFunction, imp_index,
    ])
    .exportFunc();
const wasm_module = builder.toModule();
worker.postMessage(wasm_module);
assertTrue(worker.getMessage() > 0);
