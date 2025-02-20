// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const worker = new Worker(function() {
  onmessage = function() {
    const table_descriptor = {
      "element": "anyfunc",
      "initial": 1,
    };
    table_descriptor[Symbol.toPrimitive] = eval;
    table_descriptor.address = table_descriptor;
    try {
      new WebAssembly.Table(table_descriptor);
    } catch (e) {
      if (e instanceof ReferenceError) {
        postMessage('OK');
      } else {
        throw e;
      }
    }
  };
  performance.measureMemory();
  return Object.defineProperty(this.d8.__proto__, 'then', {'get': onmessage});
}, {'type': 'function'});
worker.getMessage();
