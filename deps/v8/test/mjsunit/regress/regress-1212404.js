// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const barrier_arr = new Int32Array(new SharedArrayBuffer(4));
function onmessage({data:[data_arr, barrier_arr]}) {
  Atomics.store(barrier_arr, 0, 1);
  for (let nr = 1; nr < 1000; ++nr) {
    for (let idx = 0; idx < data_arr.length; ++idx) {
      data_arr[idx] = nr;
    }
  }
}

const worker = new Worker(`onmessage = ${onmessage}`, {type: 'string'});
const data_arr = new Int32Array(new SharedArrayBuffer(1024));
worker.postMessage([data_arr, barrier_arr]);
while (Atomics.load(barrier_arr) == 0) { }
data_arr.reverse();
