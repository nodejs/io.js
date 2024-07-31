// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// Test debug events when an exception is thrown inside a Promise,
// which is caught by a custom promise, which throws a new exception
// in its reject handler. We expect no Exception debug events.

Debug = debug.Debug;

var expected_events = 2;
var log = [];

var p = new Promise(function(resolve, reject) {
  log.push("resolve");
  resolve();
});

function MyPromise(resolver) {
  var reject = function() {
    throw new Error("reject");  // event
  };
  var resolve = function() { };
  resolver(resolve, reject);
};

MyPromise.prototype = new Promise(function() {});
MyPromise.__proto__ = Promise;
p.constructor = MyPromise;

var q = p.then(
  function() {
    log.push("throw caught");
    throw new Error("caught");  // event
  });

function listener(event, exec_state, event_data, data) {
  try {
    if (event == Debug.DebugEvent.Exception) {
      if (expected_events === 0) {
        assertUnreachable();
      } else {
        expected_events--;
        log.push("uncaught exception");
      }
    }
  } catch (e) {
    %AbortJS(e + "\n" + e.stack);
  }
}

Debug.setBreakOnUncaughtException();
Debug.setListener(listener);

log.push("end main");

function testDone(iteration) {
  function checkResult() {
    try {
      assertTrue(iteration < 10);
      if (expected_events === 0) {
        assertEquals(["resolve", "end main",
                      "throw caught", "uncaught exception", "uncaught exception"], log);
      } else {
        testDone(iteration + 1);
      }
    } catch (e) {
      %AbortJS(e + "\n" + e.stack);
    }
  }

  %EnqueueMicrotask(checkResult);
}

testDone(0);
