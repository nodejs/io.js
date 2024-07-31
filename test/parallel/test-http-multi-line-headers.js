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
const assert = require('assert');

const http = require('http');
const net = require('net');

const server = net.createServer(function(conn) {
  const body = 'Yet another node.js server.';

  const response =
      'HTTP/1.1 200 OK\r\n' +
      'Connection: close\r\n' +
      `Content-Length: ${body.length}\r\n` +
      'Content-Type: text/plain;\r\n' +
      ' x-unix-mode=0600;\r\n' +
      ' name="hello.txt"\r\n' +
      '\r\n' +
      body;

  conn.end(response);
  server.close();
});

server.listen(0, common.mustCall(function() {
  http.get({
    host: '127.0.0.1',
    port: this.address().port,
    insecureHTTPParser: true
  }, common.mustCall(function(res) {
    assert.strictEqual(res.headers['content-type'],
                       'text/plain; x-unix-mode=0600; name="hello.txt"');
    res.destroy();
  }));
}));
