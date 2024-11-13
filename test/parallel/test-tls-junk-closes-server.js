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

if (!common.hasCrypto)
  common.skip('missing crypto');

const tls = require('tls');
const net = require('net');

const options = {
  key: fixtures.readKey('agent2-key.pem'),
  cert: fixtures.readKey('agent2-cert.pem')
};

const server = tls.createServer(options, common.mustNotCall());

server.listen(0, common.mustCall(function() {
  const c = net.createConnection(this.address().port);

  c.on('data', function() {
    // We must consume all data sent by the server. Otherwise the
    // end event will not be sent and the test will hang.
    // For example, when compiled with OpenSSL32 we see the
    // following response '15 03 03 00 02 02 16' which
    // decodes as a fatal (0x02) TLS error alert number 22 (0x16),
    // which corresponds to TLS1_AD_RECORD_OVERFLOW which matches
    // the error we see if NODE_DEBUG is turned on.
    // Some earlier OpenSSL versions did not seem to send a response
    // but the TLS spec seems to indicate there should be one
    // https://datatracker.ietf.org/doc/html/rfc8446#page-85
    // and error handling seems to have been re-written/improved
    // in OpenSSL32. Consuming the data allows the test to pass
    // either way.
  });

  c.on('connect', common.mustCall(function() {
    c.write('blah\nblah\nblah\n');
  }));

  c.on('end', common.mustCall(function() {
    server.close();
  }));
}));
