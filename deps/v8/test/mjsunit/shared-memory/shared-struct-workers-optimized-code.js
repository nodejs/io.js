// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --shared-string-table --harmony-struct --allow-natives-syntax
// Flags: --verify-heap

"use strict";

if (this.Worker) {

(function TestSharedStructOptimizedCode() {
  let workerScript =
      `onmessage = function({data:struct}) {
         %PrepareFunctionForOptimization(writePayload);
         for (let i = 0; i < 5; i++) writePayload(struct);
         %OptimizeFunctionOnNextCall(writePayload);
         writePayload(struct);
         postMessage("done");
       };
       function writePayload(struct) {
         for (let i = 0; i < 100; i++) {
           struct.payload = struct.payload + 1;
         }
       }
       postMessage("started");`;

  let worker = new Worker(workerScript, { type: 'string' });
  assertEquals("started", worker.getMessage());

  let MyStruct = new SharedStructType(['payload']);
  let struct = new MyStruct();
  struct.payload = 0;
  worker.postMessage(struct);
  assertEquals("done", worker.getMessage());

  worker.terminate();
})();

}
