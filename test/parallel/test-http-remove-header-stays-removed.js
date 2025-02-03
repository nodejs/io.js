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

const server = http.createServer(common.mustCall(function(request, response) {
  const socket = response.socket;

  // Removed headers should stay removed, even if node automatically adds them
  // to the output:
  response.removeHeader('connection');
  response.removeHeader('transfer-encoding');
  response.removeHeader('content-length');

  // Make sure that removing and then setting still works:
  response.removeHeader('date');
  response.setHeader('date', 'coffee o clock');

  response.on('finish', common.mustCall(function() {
    // The socket should be closed immediately, with no keep-alive, because
    // no content-length or transfer-encoding are used.
    assert.strictEqual(socket.writableEnded, true);
  }));

  response.end('beep boop\n');
}));

server.listen(0, function() {
  http.get({ port: this.address().port }, function(res) {
    assert.strictEqual(res.statusCode, 200);
    assert.deepStrictEqual(res.headers, { date: 'coffee o clock' });

    let response = '';
    res.setEncoding('ascii');
    res.on('data', function(chunk) {
      response += chunk;
    });

    res.on('end', function() {
      assert.strictEqual(response, 'beep boop\n');
      server.close();
    });
  });
});
