// Copyright 2015 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Test the Worker API of d8.  This test only makes sense with d8. A Worker
// spawns a new OS thread and isolate, and runs it concurrently with the
// current running thread.

var workerScript =
  `postMessage('Starting worker');
// Set a global variable; should not be visible outside of the worker's
// context.
   foo = 100;
   var c = 0;
   onmessage = function({data:m}) {
     switch (c++) {
       case 0:
         if (m !== undefined) throw new Error('undefined');
         break;
       case 1:
         if (m !== null) throw new Error('null');
         break;
       case 2:
         if (m !== true) throw new Error('true');
         break;
       case 3:
         if (m !== false) throw new Error('false');
         break;
       case 4:
         if (m !== 100) throw new Error('Number');
         break;
       case 5:
         if (m !== 'hi') throw new Error('String');
         break;
       case 6:
         if (JSON.stringify(m) !== '[4,true,\"bye\"]') {
           throw new Error('Array');
         }
         break;
       case 7:
         if (JSON.stringify(m) !== '{"a":1,"b":2.5,"c":"three"}')
           throw new Error('Object' + JSON.stringify(m));
         break;
       case 8:
         var ab = m;
         var t = new Uint32Array(ab);
         if (ab.byteLength !== 16)
           throw new Error('ArrayBuffer clone byteLength');
         for (var i = 0; i < 4; ++i)
           if (t[i] !== i)
             throw new Error('ArrayBuffer clone value ' + i);
         break;
       case 9:
         var ab = m;
         var t = new Uint32Array(ab);
         if (ab.byteLength !== 32)
           throw new Error('ArrayBuffer transfer byteLength');
         for (var i = 0; i < 8; ++i)
           if (t[i] !== i)
             throw new Error('ArrayBuffer transfer value ' + i);
         break;
      case 10:
        if (JSON.stringify(m) !== '{"foo":{},"err":{}}')
          throw new Error('Object ' + JSON.stringify(m));
        break;
      case 11:
        if (m.message != "message")
          throw new Error('Error ' + JSON.stringify(m));
        break;
     }
     if (c == 12) {
       postMessage('DONE');
     }
   };`;

if (this.Worker) {
  function createArrayBuffer(byteLength) {
    var ab = new ArrayBuffer(byteLength);
    var t = new Uint32Array(ab);
    for (var i = 0; i < byteLength / 4; ++i)
      t[i] = i;
    return ab;
  }

  assertThrows(function() {
    // Second arg must be 'options' object
    new Worker(workerScript, 123);
  });

  assertThrows(function() {
    new Worker('test/mjsunit/d8/d8-worker.js', {type: 'invalid'});
  });

  assertThrows(function() {
    // worker type defaults to 'classic' which tries to load from file
    new Worker(workerScript);
  });

  var w = new Worker(workerScript, {type: 'string'});

  assertEquals("Starting worker", w.getMessage());

  w.postMessage(undefined);
  w.postMessage(null);
  w.postMessage(true);
  w.postMessage(false);
  w.postMessage(100);
  w.postMessage("hi");
  w.postMessage([4, true, "bye"]);
  w.postMessage({a: 1, b: 2.5, c: "three"});

  // Test bad get in transfer list.
  var transferList = [undefined];
  Object.defineProperty(transferList, '0', {
    get: function() {
      throw 'unexpected!';
    }
  });
  assertThrows(function() {
    w.postMessage([], transferList);
  });

  // Test console.log in transfer list.
  (function() {
    var val = [undefined];
    Object.defineProperty(val, '0', {get: () => console.log()});
    assertThrows(function() {
      w.postMessage(val, [val]);
    });
  })();

  // Clone ArrayBuffer
  var ab1 = createArrayBuffer(16);
  w.postMessage(ab1);
  assertEquals(16, ab1.byteLength);  // ArrayBuffer should not be detached.

  // Transfer ArrayBuffer
  var ab2 = createArrayBuffer(32);
  w.postMessage(ab2, [ab2]);
  assertEquals(0, ab2.byteLength);  // ArrayBuffer should be detached.

  // Attempting to transfer the same ArrayBuffer twice should throw.
  assertThrows(function() {
    var ab3 = createArrayBuffer(4);
    w.postMessage(ab3, [ab3, ab3]);
  });

  assertEquals("undefined", typeof foo);

  // Transfer Error
  const err = new Error();
  w.postMessage({ foo: err, err })

  // Transfer single Error
  w.postMessage(new Error("message"))

  // Read a message from the worker.
  assertEquals("DONE", w.getMessage());

  w.terminate();


  // Make sure that the main thread doesn't block forever in getMessage() if
  // the worker dies without posting a message.
  var w2 = new Worker('', {type: 'string'});
  var msg = w2.getMessage();
  assertEquals(undefined, msg);

  // Test close (should send termination to worker isolate).
  var close_worker = new Worker(
      "postMessage('one'); close(); while(true) { postMessage('two'); }",
      {type: "string"})
  assertEquals("one", close_worker.getMessage());
}
