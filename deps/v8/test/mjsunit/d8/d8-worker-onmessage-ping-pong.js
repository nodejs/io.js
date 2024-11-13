// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const worker = new Worker(function() {
  onmessage = function(e) {
    // No mjsunit.js in the worker scope, so no assertEquals.
    // TODO(leszeks): Make importScripts work with paths relative to the
    // worker or caller script.
    // assertEquals({text: "ping"}, e.data);
    print(JSON.stringify(e.data));

    postMessage({text: "pong", reply_to: e.data});
    close();
  }
}, {type: 'function'});

worker.postMessage({text: "ping"});
worker.onmessage = function(e) {
  print(JSON.stringify(e.data));

  assertEquals({text: "pong", reply_to: {text: "ping"}}, e.data);
};
