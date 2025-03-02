// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const int32_arr = new Int32Array(new SharedArrayBuffer(4));
const workerScript = function() {
  onmessage = function({data: buf}) {
    const arr = new Int32Array(buf);
    for (let i = 1; i < 100; ++i) {
      arr.fill(i);
    }
  };
};
const worker = new Worker(workerScript, {type: 'function'});
worker.postMessage(int32_arr.buffer);
while (Atomics.load(int32_arr) == 0) {}
assertThrows(
    () => new WebAssembly.Module(int32_arr), WebAssembly.CompileError,
    /expected magic word/);
