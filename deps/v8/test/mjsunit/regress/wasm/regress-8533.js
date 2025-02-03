// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');


// In  this test we start a worker which enters wasm and stays there in a loop.
// The main thread stays in JS and checks that its thread-in-wasm flag is not
// set. The main thread calls setTimeout after every check to give the worker a
// chance to be scheculed.
const sync_address = 12;
(function TestPostModule() {
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_v_v);
  let import_id = builder.addImport('m', 'func', sig_index);
  builder.addFunction('wait', kSig_v_v)
      .addBody([
        // Calling the imported function sets the thread-in-wasm flag of the
        // main thread.
        kExprCallFunction, import_id,  // --
        kExprLoop, kWasmVoid,          // --
        kExprI32Const, sync_address,   // --
        kExprI32LoadMem, 0, 0,         // --
        kExprI32Eqz,
        kExprBrIf, 0,                  // --
        kExprEnd,
      ])
      .exportFunc();

  builder.addFunction('signal', kSig_v_v)
      .addBody([
        kExprI32Const, sync_address,  // --
        kExprI32Const, 1,             // --
        kExprI32StoreMem, 0, 0,       // --
        ])
      .exportFunc();
  builder.addImportedMemory("m", "imported_mem", 0, 1, "shared");

  let module = builder.toModule();
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});

  let workerScript = `
    onmessage = function({data:msg}) {
      try {
        let worker_instance = new WebAssembly.Instance(msg.module,
            {m: {imported_mem: msg.memory,
                 func: _ => 5}});
        postMessage("start running");
        worker_instance.exports.wait();
        postMessage("finished");
      } catch(e) {
        postMessage('ERROR: ' + e);
      }
    }
  `;

  let worker = new Worker(workerScript, {type: 'string'});
  worker.postMessage({module: module, memory: memory});

  let main_instance = new WebAssembly.Instance(
      module, {m: {imported_mem: memory, func: _ => 7}});

  let counter = 0;
  function CheckThreadNotInWasm() {
    // We check the thread-in-wasm flag many times and reschedule ourselves in
    // between to increase the chance that we read the flag set by the worker.
    assertFalse(%IsThreadInWasm());
    counter++;
    if (counter < 100) {
      setTimeout(CheckThreadNotInWasm, 0);
    } else {
      main_instance.exports.signal(sync_address);
      assertEquals('finished', worker.getMessage());
      worker.terminate();
    }
  }

  assertFalse(%IsThreadInWasm());
  assertEquals('start running', worker.getMessage());
  CheckThreadNotInWasm();
})();
