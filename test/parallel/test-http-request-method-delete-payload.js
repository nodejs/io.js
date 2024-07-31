'use strict';
const common = require('../common');

const assert = require('assert');
const http = require('http');

const data = 'PUT / HTTP/1.1\r\n\r\n';

const server = http.createServer(common.mustCall(function(req, res) {
  req.on('data', function(chunk) {
    assert.strictEqual(chunk, Buffer.from(data));
  });
  res.setHeader('Content-Type', 'text/plain');
  for (let i = 0; i < req.rawHeaders.length; i += 2) {
    if (req.rawHeaders[i].toLowerCase() === 'host') continue;
    if (req.rawHeaders[i].toLowerCase() === 'connection') continue;
    res.write(`${req.rawHeaders[i]}: ${req.rawHeaders[i + 1]}\r\n`);
  }
  res.end();
})).unref();

server.listen(0, common.mustCall(() => {
  const port = server.address().port;
  const req = http.request({ method: 'DELETE', port }, function(res) {
    res.resume();
  });

  req.write(data);
  req.end();
}));
