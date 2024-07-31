// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function workerCode() {
  onmessage = function() {
    this.performance.measureMemory();
    postMessage("done");
  };
  Object.defineProperty(this.d8.__proto__, 'then', {'get': function() {}});
}
const worker = new Worker(workerCode, {
  'type': 'function',
});
worker.postMessage({});
worker.getMessage();
