// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let sab = new SharedArrayBuffer(40);
let i32arr = new Int32Array(sab);
let worker = new Worker(
    'onmessage = function({data:memory}) { while (memory[1] == 0) {} };',
    {type: 'string'});
worker.postMessage(i32arr);
i32arr.copyWithin(Array(0x8000).fill("a"), 0);
i32arr[1] = 1;
