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
const common = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('assert');

if (!common.hasCrypto)
  common.skip('missing crypto');

const tls = require('tls');

let key = fixtures.readKey('rsa_private.pem');
let cert = fixtures.readKey('rsa_cert.crt');

// This test validates that we accept certificates and keys which
// do not end with a newline. If a newline exists at the end
// of the key or cert being used remove it
let i = 0;
while (key[key.length - 1 - i] === 0x0a) i++;
if (i !== 0) key = key.slice(0, key.length - i);

i = 0;
while (cert[cert.length - 1 - i] === 0x0a) i++;
if (i !== 0) cert = cert.slice(0, cert.length - i);

function test(cert, key, cb) {
  assert.notStrictEqual(cert.at(-1), 0x0a);
  assert.notStrictEqual(key.at(-1), 0x0a);
  const server = tls.createServer({
    cert,
    key
  }).listen(0, function() {
    server.close(cb);
  });
}

test(cert, key, common.mustCall(function() {
  test(Buffer.from(cert), Buffer.from(key), common.mustCall());
}));
