// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Try to catch TSAN issues with access to SharedArrayBuffer.

function onmessage({data:[buf]}) {
  const arr = new Int32Array(buf);
  for (let val = 1; val < 100; ++val) arr.fill(val);
}
const arr = new Int32Array(new SharedArrayBuffer(4));
const worker = new Worker(`onmessage = ${onmessage}`, {type: 'string'});
worker.postMessage([arr.buffer]);
// Wait until the worker starts filling the array.
while (Atomics.load(arr) == 0) { }
// Try setting a value on the shared array buffer that races with the fill.
arr.set(arr);
