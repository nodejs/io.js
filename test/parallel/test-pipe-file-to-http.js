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
const fs = require('fs');
const http = require('http');

const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

const filename = tmpdir.resolve('big');
let count = 0;

const server = http.createServer((req, res) => {
  assert.strictEqual(req.method, 'POST');
  req.pause();

  const timeoutId = setTimeout(() => {
    req.resume();
  }, 1000);

  req.on('data', (chunk) => {
    count += chunk.length;
  });

  req.on('end', () => {
    if (timeoutId) {
      clearTimeout(timeoutId);
    }
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end();
  });
});
server.listen(0);

server.on('listening', () => {

  // Create a zero-filled file
  const fd = fs.openSync(filename, 'w');
  fs.ftruncateSync(fd, 10 * 1024 * 1024);
  fs.closeSync(fd);

  makeRequest();
});

function makeRequest() {
  const req = http.request({
    port: server.address().port,
    path: '/',
    method: 'POST'
  });

  const s = fs.ReadStream(filename);
  s.pipe(req);
  s.on('close', common.mustSucceed());

  req.on('response', (res) => {
    res.resume();
    res.on('end', () => {
      server.close();
    });
  });
}

process.on('exit', () => {
  assert.strictEqual(count, 1024 * 10240);
});
