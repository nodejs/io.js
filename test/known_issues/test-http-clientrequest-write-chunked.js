'use strict';

// Refs: https://github.com/nodejs/node/pull/34066

const common = require('../common');
const assert = require('assert');
const http = require('http');

// Test that ClientRequest#write with default options
// uses a chunked Transfer-Encoding
const upload = 'PUT / HTTP/1.1\r\n\r\n';
const response = 'transfer-encoding: chunked\r\n';

// Test that the upload is properly received with the same headers,
// regardless of request method.
const methods = [ 'GET', 'HEAD', 'DELETE', 'POST', 'PATCH', 'PUT', 'OPTIONS' ];

const server = http.createServer(common.mustCall(function(req, res) {
  req.on('data', function(chunk) {
    assert.strictEqual(chunk.toString(), upload);
  });
  res.setHeader('Content-Type', 'text/plain');
  res.write(`${req.method}\r\n`);
  for (let i = 0; i < req.rawHeaders.length; i += 2) {
    // Ignore a couple headers that may vary
    if (req.rawHeaders[i].toLowerCase() === 'host') continue;
    if (req.rawHeaders[i].toLowerCase() === 'connection') continue;
    res.write(`${req.rawHeaders[i]}: ${req.rawHeaders[i + 1]}\r\n`);
  }
  res.end();
}), methods.length);

server.listen(0, function tryNextRequest() {
  const method = methods.pop();
  if (method === undefined) return;
  const port = server.address().port;
  const req = http.request({ method, port }, function(res) {
    const chunks = [];
    res.on('data', function(chunk) {
      chunks.push(chunk);
    });
    res.on('end', function() {
      const received = Buffer.concat(chunks).toString();
      const expected = method.toLowerCase() + '\r\n' + response;
      assert.strictEqual(received.toLowerCase(), expected);
      tryNextRequest();
    });
  });

  req.write(upload);
  req.end();
}).unref();
