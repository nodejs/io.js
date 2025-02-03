// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const N = 10;

function workerCode(N) {
  const sab = new SharedArrayBuffer(16);
  const i32a = new Int32Array(sab);
  const location = 0;
  const expected_value = 0;

  function start() {
    // Create N async waiters; the even ones without timeout and the odd ones
    // with timeout.
    for (let i = 0; i < N; ++i) {
      let result;
      if (i % 2 == 0) {
        result = Atomics.waitAsync(i32a, location, expected_value);
      } else {
        result = Atomics.waitAsync(i32a, location, expected_value, i);
      }
      result.value.then(
        (value) => { postMessage(value + " " + i); },
        () => { postMessage("unexpected"); });
    }
  }

  function wakeUpRemainingWaiters() {
    // Wake up all waiters
    let notify_return_value = Atomics.notify(i32a, location);
    postMessage("notify return value " + notify_return_value);
  }

  onmessage = function({data:param}) {
    if (param == "start") {
      start();
    } else if (param == "wakeUpRemainingWaiters") {
      wakeUpRemainingWaiters();
    }
  };
}

const w = new Worker(workerCode, {type: 'function', arguments: [N]});
w.postMessage("start");

// Verify that all timed out waiters timed out in timeout order.
let waiter_no = 1;
for (let i = 0; i < N / 2; ++i) {
  const m = w.getMessage();
  assertEquals("timed-out " + waiter_no, m);
  waiter_no += 2;
}
w.postMessage("wakeUpRemainingWaiters");
const m = w.getMessage();
assertEquals("notify return value " + N / 2, m);

// Verify that the waiters woke up in FIFO order.
waiter_no = 0;
for (let i = 0; i < N / 2; ++i) {
  const m = w.getMessage();
  assertEquals("ok " + waiter_no, m);
  waiter_no += 2;
}
w.terminate();
