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
if (!common.hasCrypto)
  common.skip('missing crypto');

const { onGC } = require('../common/gc');
const assert = require('assert');
const tls = require('tls');
const fixtures = require('../common/fixtures');

// Test that the implicit listener for an 'connect' event on tls.Sockets is
// added using `once()`, i.e. can be gc'ed once that event has occurred.

const server = tls.createServer({
  cert: fixtures.readKey('rsa_cert.crt'),
  key: fixtures.readKey('rsa_private.pem')
}).listen(0);

let collected = false;
const gcListener = { ongc() { collected = true; } };

{
  const gcObject = {};
  onGC(gcObject, gcListener);

  const sock = tls.connect(
    server.address().port,
    { rejectUnauthorized: false },
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
