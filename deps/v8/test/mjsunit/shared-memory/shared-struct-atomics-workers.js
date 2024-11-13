// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --shared-string-table --harmony-struct

"use strict";

if (this.Worker) {

(function TestSharedStructPostMessage() {
  let workerScript =
      `onmessage = function({data:struct}) {
         // Non-atomic write that will be made visible once main thread
         // observes the atomic write below.
         struct.struct_field.payload = 42;
         Atomics.store(struct, "string_field", "worker");
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
  // Spin until we observe the worker's write of string_field.
  while (Atomics.load(struct, "string_field") !== "worker") {}
  // The non-atomic store write must also be visible.
  assertEquals(42, struct.struct_field.payload);

  worker.terminate();
})();

(function TestCompareExchange() {
  // Test that we can perform compareExchange simultaneously on 2 threads.
  // One thread will update the value only after the other thread has
  // performed an update.

  const ST = new SharedStructType(['field']);
  const s1 = new ST();
  const s = new ST();

  // Odd number of values so the main thread perfoms the last compareExchange.
  let testValues =
      [0, 1.5, undefined, null, true, false, 'foo', s1, new SharedArray(1)];
  let sharedtestValues = new SharedArray(testValues.length);
  Object.assign(sharedtestValues, testValues);

  let workerScript = `onmessage = function({data:e}) {
    const s = e[0];
    let i = 0;
    const testValues = e[1];
    while(i < testValues.length - 1) {
      let value = testValues[i];
      let nextValue = testValues[i+1];
      if (value === Atomics.compareExchange(s, 'field', value, nextValue)) {
        i += 2;
      }
    }
  }`

  let worker = new Worker(workerScript, {type: 'string'});
  worker.postMessage([s, sharedtestValues]);
  s.field = testValues[0];
  let i = 1;
  while (i < testValues.length - 1) {
    let value = testValues[i];
    let nextValue = testValues[i + 1];
    if (value === Atomics.compareExchange(s, 'field', value, nextValue)) {
      i += 2;
    }
  }

  assertEquals(testValues[testValues.length - 1], s.field);
  worker.terminate();
})();
}
