// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --shared-string-table --allow-natives-syntax
// Flags: --expose-gc

if (this.Worker) {

(function TestSharedStringPostMessage() {
  function workerCode() {
    let Box = new SharedStructType(['payload']);
    let b1 = new Box();
    b1.payload = "started";
    postMessage(b1);
    onmessage = function({data:box}) {
      if (!%IsSharedString(box.payload)) {
        throw new Error("str isn't shared");
      }
      let b2 = new Box();
      b2.payload = box.payload;
      postMessage(b2);
    };
  }

  // Strings referenced in shared structs are serialized by sharing.

  let worker = new Worker(workerCode, { type: 'function' });
  let started = worker.getMessage();
  assertTrue(%IsSharedString(started.payload));
  assertEquals("started", started.payload);

  let Box = new SharedStructType(['payload']);
  let box_to_send = new Box();
  // Create a string in large object space.
  let payload = %FlattenString('a'.repeat(1024 * 256));
  assertTrue(%InLargeObjectSpace(payload));
  // Trigger a gc to move the object to old space.
  gc();
  assertFalse(%InYoungGeneration(payload));
  box_to_send.payload = payload;
  assertTrue(%IsSharedString(box_to_send.payload));
  worker.postMessage(box_to_send);
  let box_received = worker.getMessage();
  assertTrue(%IsSharedString(box_received.payload));
  assertFalse(box_to_send === box_received);
  // Object.is and === won't check pointer equality of Strings.
  assertTrue(%IsSameHeapObject(box_to_send.payload, box_received.payload));

  worker.terminate();
})();

}
