// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

$262.agent = (function () {

var workers = [];
var i32a = null;
var pendingReports = [];

// Agents call Atomics.wait on this location to sleep.
var SLEEP_LOC = 0;
// 1 if the started worker is ready, 0 otherwise.
var START_LOC = 1;
// The number of workers that have received the broadcast.
var BROADCAST_LOC = 2;
// Each worker has a count of outstanding reports; worker N uses memory
// location [WORKER_REPORT_LOC + N].
var WORKER_REPORT_LOC = 3;

function workerScript(script) {
  return `
    var index;
    var i32a = null;
    var broadcasts = [];
    var pendingReceiver = null;

    function handleBroadcast() {
      if (pendingReceiver && broadcasts.length > 0) {
        pendingReceiver.apply(null, broadcasts.shift());
        pendingReceiver = null;
      }
    };

    var onmessage = function({data:msg}) {
      switch (msg.kind) {
        case 'start':
          i32a = msg.i32a;
          index = msg.index;
          (0, eval)(\`${script}\`);
          break;

        case 'broadcast':
          Atomics.add(i32a, ${BROADCAST_LOC}, 1);
          broadcasts.push([msg.sab, msg.id]);
          handleBroadcast();
          break;
      }
    };

    var $262 = {
      agent: {
        receiveBroadcast(receiver) {
          pendingReceiver = receiver;
          handleBroadcast();
        },

        report(msg) {
          postMessage(String(msg));
          Atomics.add(i32a, ${WORKER_REPORT_LOC} + index, 1);
        },

        sleep(s) { Atomics.wait(i32a, ${SLEEP_LOC}, 0, s); },

        leaving() {},

        monotonicNow() {
          return performance.now();
        }
      }
    };`;
}

var agent = {
  start(script) {
    if (i32a === null) {
      i32a = new Int32Array(new SharedArrayBuffer(256));
    }
    var w = new Worker(workerScript(script), {type: 'string'});
    w.index = workers.length;
    w.postMessage({kind: 'start', i32a: i32a, index: w.index});
    workers.push(w);
  },

  broadcast(sab, id) {
    if (!(sab instanceof SharedArrayBuffer)) {
      throw new TypeError('sab must be a SharedArrayBuffer.');
    }

    Atomics.store(i32a, BROADCAST_LOC, 0);

    for (var w of workers) {
      w.postMessage({kind: 'broadcast', sab: sab, id: id|0});
    }

    while (Atomics.load(i32a, BROADCAST_LOC) != workers.length) {}
  },

  getReport() {
    for (var w of workers) {
      while (Atomics.load(i32a, WORKER_REPORT_LOC + w.index) > 0) {
        pendingReports.push(w.getMessage());
        Atomics.sub(i32a, WORKER_REPORT_LOC + w.index, 1);
      }
    }

    return pendingReports.shift() || null;
  },

  sleep(s) { Atomics.wait(i32a, SLEEP_LOC, 0, s); },

  monotonicNow() {
    return performance.now();
  }
};
return agent;

})();
