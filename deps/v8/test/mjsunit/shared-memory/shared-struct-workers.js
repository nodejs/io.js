// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --shared-string-table --harmony-struct --allow-natives-syntax

"use strict";

if (this.Worker) {

(function TestSharedStructPostMessage() {
  let workerScript =
      `onmessage = function({data:struct}) {
         struct.struct_field.payload = 42;
         struct.string_field = "worker";
         postMessage("done");
       };
       postMessage("started");`;

  let worker = new Worker(workerScript, { type: 'string' });
  let started = worker.getMessage();
  assertEquals("started", started);

  let OuterStruct = new SharedStructType(['string_field', 'struct_field']);
  let InnerStruct = new SharedStructType(['payload']);
  let struct = new OuterStruct();
  struct.struct_field = new InnerStruct();
  struct.string_field = "main";
  assertEquals("main", struct.string_field);
  assertEquals(undefined, struct.struct_field.payload);
  worker.postMessage(struct);
  assertEquals("done", worker.getMessage());
  assertEquals("worker", struct.string_field);
  assertEquals(42, struct.struct_field.payload);

  worker.terminate();
})();

(function TestObjectAssign() {
  function f() {
    const Struct = new SharedStructType(['field'])
    const shared_struct = new Struct();
    const obj = {'field': 1};
    Object.assign(shared_struct, obj);
    postMessage(shared_struct.field);
  }

  const worker = new Worker(f, {'type': 'function'});
  assertEquals(1, worker.getMessage());

  worker.terminate();
})();
}
