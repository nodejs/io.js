// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --shared-string-table --harmony-struct
// --allow-natives-syntax

'use strict';

if (this.Worker) {
  (function TestSharedArrayPostMessage() {
    let workerScript = `onmessage = function({data:arr}) {
         arr[0][0] = 42;
         arr[1] = "worker";
         arr[2].payload = "updated";
         postMessage("done");
       };
       postMessage("started");`;

    let worker = new Worker(workerScript, {type: 'string'});
    let started = worker.getMessage();
    assertEquals('started', started);

    let outer_arr = new SharedArray(3);
    outer_arr[0] = new SharedArray(1);
    outer_arr[1] = 'main';
    let Struct = new SharedStructType(['payload']);
    let struct = new Struct();
    struct.payload = 'original';
    outer_arr[2] = struct;
    assertEquals('main', outer_arr[1]);
    assertEquals(undefined, outer_arr[0][0]);
    worker.postMessage(outer_arr);
    assertEquals('done', worker.getMessage());
    assertEquals('worker', outer_arr[1]);
    assertEquals(42, outer_arr[0][0]);
    assertEquals('updated', outer_arr[2].payload);

    worker.terminate();
  })();

  (function TestObjectAssign() {
    function f() {
      const shared_array = new SharedArray(1);
      const array = new Array(1);
      array[0] = 1;
      Object.assign(shared_array, array);
      postMessage(shared_array[0]);
    }

    const worker = new Worker(f, {'type': 'function'});
    assertEquals(1, worker.getMessage());

    worker.terminate();
  })();
}
