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

// Flags: --expose-internals
'use strict';
const common = require('../common');
const { hasMultiLocalhost } = require('../common/net');
if (!hasMultiLocalhost()) {
  common.skip('platform-specific test.');
}

const http = require('http');
const assert = require('assert');

const server = http.createServer((req, res) => {
  console.log(`Connect from: ${req.connection.remoteAddress}`);
  assert.strictEqual(req.connection.remoteAddress, '127.0.0.2');

  req.on('end', () => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end(`You are from: ${req.connection.remoteAddress}`);
  });
  req.resume();
});

server.listen(0, '127.0.0.1', () => {
  const options = { host: 'localhost',
                    port: server.address().port,
                    family: 4,
                    path: '/',
                    method: 'GET',
                    localAddress: '127.0.0.2' };

  const req = http.request(options, function(res) {
    res.on('end', () => {
      server.close();
    });
    res.resume();
  });
  req.end();
});
