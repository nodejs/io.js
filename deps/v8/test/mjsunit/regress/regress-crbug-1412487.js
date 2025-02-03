// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const arr = [];
const obj0 = {a : 1};
obj0.a1 = 2;
obj0.c = {};

const obj = {a : 1};
obj.a1 = 2;
obj.c = arr;

const f1 = {a : 1};
f1.a1 = 1.1;
arr.push(f1);

for (i = 0; i < 1600; i++) {
  const tmp = {a : 1};
  tmp.a1 = 1;
  tmp['b' + i] = 2;
  arr.push(tmp);
}

function workerCode() {
  onmessage = function({data:data}) {
    postMessage('done');
  }
}

worker = new Worker(workerCode, {type: 'function', arguments: []});
worker.postMessage([obj0, obj]);

assertEquals('done', worker.getMessage());
