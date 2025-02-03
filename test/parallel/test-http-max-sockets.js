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
require('../common');

// Make sure http server doesn't wait for socket pool to establish connections
// https://github.com/nodejs/node-v0.x-archive/issues/877

const http = require('http');
const assert = require('assert');

const N = 20;
let responses = 0;
let maxQueued = 0;

const agent = http.globalAgent;
agent.maxSockets = 10;

const server = http.createServer(function(req, res) {
  res.writeHead(200);
  res.end('Hello World\n');
});

server.listen(0, '127.0.0.1', function() {
  const { port } = server.address();
  const addrString = agent.getName({ host: '127.0.0.1', port });

  for (let i = 0; i < N; i++) {
    const options = {
      host: '127.0.0.1',
      port
    };

    const req = http.get(options, function(res) {
      if (++responses === N) {
        server.close();
      }
      res.resume();
    });

    assert.strictEqual(req.agent, agent);

    console.log(
      `Socket: ${agent.sockets[addrString].length}/${
        agent.maxSockets} queued: ${
        agent.requests[addrString] ? agent.requests[addrString].length : 0}`);

    const agentRequests = agent.requests[addrString] ?
      agent.requests[addrString].length : 0;

    if (maxQueued < agentRequests) {
      maxQueued = agentRequests;
    }
  }
});

process.on('exit', function() {
  assert.strictEqual(responses, N);
  assert.ok(maxQueued <= 10);
});
