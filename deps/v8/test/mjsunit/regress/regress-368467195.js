// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const worker = new Worker(function() {
  onmessage = function() {
    performance.measureMemory();
    postMessage("done");
  };
  performance.measureMemory();
  Object.defineProperty(this.d8.__proto__, 'then', {get: onmessage});
}, {type: 'function'});
worker.postMessage(0);
worker.getMessage();
