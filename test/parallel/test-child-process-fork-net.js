// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// This tests that a socket sent to the forked process works.
// See https://github.com/nodejs/node/commit/dceebbfa

'use strict';
const {
  mustCall,
  mustCallAtLeast,
  platformTimeout,
} = require('../common');
const assert = require('assert');
const fork = require('child_process').fork;
const net = require('net');
const debug = require('util').debuglog('test');
const count = 12;

if (process.argv[2] === 'child') {
  const needEnd = [];
  const id = process.argv[3];

  process.on('message', mustCall((m, socket) => {
    if (!socket) return;

    debug(`[${id}] got socket ${m}`);

    // Will call .end('end') or .write('write');
    socket[m](m);

    socket.resume();

    socket.on('data', mustCallAtLeast(() => {
      debug(`[${id}] socket.data ${m}`);
    }));

    socket.on('end', mustCall(() => {
      debug(`[${id}] socket.end ${m}`);
    }));

    // Store the unfinished socket
    if (m === 'write') {
      needEnd.push(socket);
    }

    socket.on('close', mustCall((had_error) => {
      debug(`[${id}] socket.close ${had_error} ${m}`);
    }));

    socket.on('finish', mustCall(() => {
      debug(`[${id}] socket finished ${m}`);
    }));
  }, 4));

  process.on('message', mustCall((m) => {
    if (m !== 'close') return;
    debug(`[${id}] got close message`);
    needEnd.forEach((endMe, i) => {
      debug(`[${id}] ending ${i}/${needEnd.length}`);
      endMe.end('end');
    });
  }, 4));

  process.on('disconnect', mustCall(() => {
    debug(`[${id}] process disconnect, ending`);
    needEnd.forEach((endMe, i) => {
      debug(`[${id}] ending ${i}/${needEnd.length}`);
      endMe.end('end');
    });
  }));

} else {

  const child1 = fork(process.argv[1], ['child', '1']);
  const child2 = fork(process.argv[1], ['child', '2']);
  const child3 = fork(process.argv[1], ['child', '3']);

  const server = net.createServer();

  let connected = 0;
  let closed = 0;
  server.on('connection', function(socket) {
    switch (connected % 6) {
      case 0:
        child1.send('end', socket); break;
      case 1:
        child1.send('write', socket); break;
      case 2:
        child2.send('end', socket); break;
      case 3:
        child2.send('write', socket); break;
      case 4:
        child3.send('end', socket); break;
      case 5:
        child3.send('write', socket); break;
    }
    connected += 1;

    // TODO(@jasnell): This is not actually being called.
    // It is not clear if it is needed.
    socket.once('close', () => {
      debug(`[m] socket closed, total ${++closed}`);
    });

    if (connected === count) {
      closeServer();
    }
  });

  let disconnected = 0;
  server.on('listening', mustCall(() => {

    let j = count;
    while (j--) {
      const client = net.connect(server.address().port, '127.0.0.1');
      client.on('error', () => {
        // This can happen if we kill the subprocess too early.
        // The client should still get a close event afterwards.
        // It likely won't so don't wrap in a mustCall.
        debug('[m] CLIENT: error event');
      });
      client.on('close', mustCall(() => {
        debug('[m] CLIENT: close event');
        disconnected += 1;
      }));
      client.resume();
    }
  }));

  let closeEmitted = false;
  server.on('close', mustCall(function() {
    closeEmitted = true;

    // Clean up child processes.
    try {
      child1.kill();
    } catch {
      debug('child process already terminated');
    }
    try {
      child2.kill();
    } catch {
      debug('child process already terminated');
    }
    try {
      child3.kill();
    } catch {
      debug('child process already terminated');
    }
  }));

  server.listen(0, '127.0.0.1');

  function closeServer() {
    server.close();

    setTimeout(() => {
      assert(!closeEmitted);
      child1.send('close');
      child2.send('close');
      child3.disconnect();
    }, platformTimeout(200));
  }

  process.on('exit', function() {
    assert.strictEqual(server._workers.length, 0);
    assert.strictEqual(disconnected, count);
    assert.strictEqual(connected, count);
  });
}
