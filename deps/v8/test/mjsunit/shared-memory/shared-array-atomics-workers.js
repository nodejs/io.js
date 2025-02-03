// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --shared-string-table --harmony-struct

'use strict';

if (this.Worker) {
  (function TestSharedArrayPostMessage() {
    let workerScript = `onmessage = function({data:arr}) {
         // Non-atomic write that will be made visible once main thread
         // observes the atomic write below.
         arr[0][0] = 42;
         arr[1].payload = 84;
         Atomics.store(arr, 2, "worker");
       };
       postMessage("started");`;

    let worker = new Worker(workerScript, {type: 'string'});
    let started = worker.getMessage();
    assertEquals('started', started);

    let OuterArray = new SharedArray(3);
    let Struct = new SharedStructType(['payload']);
    OuterArray[0] = new SharedArray(1);
    OuterArray[1] = new Struct();
    OuterArray[2] = 'main';
    assertEquals(undefined, OuterArray[0][0]);
    assertEquals(undefined, OuterArray[1].payload);
    assertEquals('main', OuterArray[2]);
    worker.postMessage(OuterArray);
    // Spin until we observe the worker's write on index 2.
    while (Atomics.load(OuterArray, 2) !== 'worker') {
    }
    // The non-atomic store write must also be visible.
    assertEquals(42, OuterArray[0][0]);
    assertEquals(84, OuterArray[1].payload);

    worker.terminate();
  })();

  (function TestCompareExchange() {
    // Test that we can perform compareExchange simultaneously on 2 threads.
    // One thread will update the value only after the other thread has
    // performed an update.

    const ST = new SharedStructType(['field']);
    const s = new ST();
    const arr = new SharedArray(1);

    // Odd number of values so the main thread perfoms the last compareExchange.
    let testValues =
        [0, 1.5, undefined, null, true, false, 'foo', s, new SharedArray(1)];
    let sharedtestValues = new SharedArray(testValues.length);
    Object.assign(sharedtestValues, testValues);

    let workerScript = `onmessage = function({data:e}) {
      const arr = e[0];
      let i = 0;
      const testValues = e[1];
      while(i < testValues.length - 1) {
        let value = testValues[i];
        let nextValue = testValues[i+1];
        if (value === Atomics.compareExchange(arr, 0, value, nextValue)) {
          i += 2;
        }
      }
    }`

    let worker = new Worker(workerScript, {type: 'string'});
    worker.postMessage([arr, sharedtestValues]);
    arr[0] = testValues[0];
    let i = 1;
    while (i < testValues.length - 1) {
      let value = testValues[i];
      let nextValue = testValues[i + 1];
      if (value === Atomics.compareExchange(arr, 0, value, nextValue)) {
        i += 2;
      }
    }

    assertEquals(testValues[testValues.length - 1], arr[0]);
    worker.terminate();
  })();
}
