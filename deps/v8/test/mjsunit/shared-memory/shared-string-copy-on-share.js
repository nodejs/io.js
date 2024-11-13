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
  // Create a string that is not internalized by the parser. Create dummy
  // strings before and after the string to make sure we have enough live
  // objects on the page to get it promoted to old space (for minor MS).
  let trash = [];
  for (let i = 0; i < 1024 * 32; i++) {
    trash.push('a'.repeat(8));
  }
  let payload = %FlattenString('a'.repeat(1024 * 60));
  for (let i = 0; i < 1024 * 32; i++) {
    trash.push('a'.repeat(8));
  }
  // Trigger two GCs to move the object to old space.
  gc({type: 'minor'});
  gc({type: 'minor'});
  assertFalse(%InLargeObjectSpace(payload));
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
