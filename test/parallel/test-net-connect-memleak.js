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

'use strict';
// Flags: --expose-gc

const common = require('../common');
const { onGC } = require('../common/gc');
const assert = require('assert');
const net = require('net');

// Test that the implicit listener for an 'connect' event on net.Sockets is
// added using `once()`, i.e. can be gc'ed once that event has occurred.

const server = net.createServer(common.mustCall()).listen(0);

let collected = false;
const gcListener = { ongc() { collected = true; } };

{
  const gcObject = {};
  onGC(gcObject, gcListener);

  const sock = net.createConnection(
    server.address().port,
    common.mustCall(() => {
      assert.strictEqual(gcObject, gcObject); // Keep reference alive
      assert.strictEqual(collected, false);
      setImmediate(done, sock);
    }));
}

function done(sock) {
  globalThis.gc();
  setImmediate(() => {
    assert.strictEqual(collected, true);
    sock.end();
    server.close();
  });
}
