// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function TestPostMessageUnsharedMemory() {
  let worker = new Worker('', {type: 'string'});
  let memory = new WebAssembly.Memory({initial: 1, maximum: 2});

  assertThrows(() => worker.postMessage(memory), Error);
})();

(function TestPostMessageUnsharedMemoryBuffer() {
  let worker = new Worker('', {type: 'string'});
  let memory = new WebAssembly.Memory({initial: 1, maximum: 2});

  worker.postMessage(memory.buffer);
  assertThrows(() => {
    worker.postMessage(memory.buffer, [memory.buffer])
  }, Error)
  assertThrows(() => {
    worker.postMessage(undefined, [memory.buffer])
  }, Error)
})();

// Can't use assert in a worker.
function workerHelpersHelper() {
  assertTrue = function(value, msg) {
    if (!value) {
      postMessage("Error: " + msg);
      throw new Error("Exit");  // To stop testing.
    }
   }

  assertIsWasmMemory = function(memory, expectedSize) {
    assertTrue(memory instanceof WebAssembly.Memory,
               "object is not a WebAssembly.Memory");

    assertTrue(memory.buffer instanceof SharedArrayBuffer,
               "object.buffer is not a SharedArrayBuffer");

    assertTrue(memory.buffer.byteLength == expectedSize,
               "object.buffer.byteLength is not " + expectedSize + " bytes");
  }
}

let workerHelpers = "(" + workerHelpersHelper.toString() + ")()";

(function TestPostMessageSharedMemory() {
  function workerCode(workerHelpers) {
    eval(workerHelpers);
    onmessage = function({data:memory}) {
      assertIsWasmMemory(memory, 65536);
      postMessage("OK");
    };
  }

  let worker = new Worker(workerCode,
                         {type: 'function', arguments: [workerHelpers]});
  let memory = new WebAssembly.Memory({initial: 1, maximum: 2, shared: true});
  worker.postMessage(memory);
  assertEquals("OK", worker.getMessage());
  worker.terminate();
})();

(function TestPostMessageComplexObjectWithSharedMemory() {
  function workerCode(workerHelpers) {
    eval(workerHelpers);
    onmessage = function({data:obj}) {
       assertIsWasmMemory(obj.memories[0], 65536);
       assertIsWasmMemory(obj.memories[1], 65536);
       assertTrue(obj.buffer instanceof SharedArrayBuffer,
                  "buffer is not a SharedArrayBuffer");
       assertTrue(obj.memories[0] === obj.memories[1], "memories aren't equal");
       assertTrue(obj.memories[0].buffer === obj.buffer,
                  "buffers aren't equal");
       assertTrue(obj.foo === 1, "foo is not 1");
       postMessage("OK");
    };
  }

  let worker = new Worker(workerCode,
                          {type: 'function', arguments: [workerHelpers]});
  let memory = new WebAssembly.Memory({initial: 1, maximum: 2, shared: true});
  let obj = {memories: [memory, memory], buffer: memory.buffer, foo: 1};
  worker.postMessage(obj);
  assertEquals("OK", worker.getMessage());
  worker.terminate();
})();

(function TestTwoWorkers() {
  function workerCode(workerHelpers) {
    eval(workerHelpers);
    onmessage = function({data:memory}) {
       assertIsWasmMemory(memory, 65536);
       postMessage("OK");
    };
  }

  let workers = [new Worker(workerCode,
                            {type: 'function', arguments: [workerHelpers]}),
                 new Worker(workerCode,
                            {type: 'function', arguments: [workerHelpers]})];
  let memory = new WebAssembly.Memory({initial: 1, maximum: 2, shared: true});
  for (let worker of workers) {
    worker.postMessage(memory);
    assertEquals("OK", worker.getMessage());
    worker.terminate();
  }
})();
