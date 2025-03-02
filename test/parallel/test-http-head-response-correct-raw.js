'use strict';
const common = require('../common');
const http = require('http');
const assert = require('assert');
const net = require('net');

// This test is to make sure that when the HTTP server
// responds to a HEAD request, it returns the correct
// raw response.

// Body that should be sent if the request were GET.
const body = 'Test body';

// Regex to test that the raw response is correct.
// Date can change.
const regex = new RegExp(
  `HTTP/1[.]1 200 OK\\r\\nContent-Type: text/plain\\r\\nDate: .+\\r\\nConnection: keep-alive\\r\\nKeep-Alive: timeout=5\\r\\nContent-Length: ${body.length}\\r\\n\\r\\n`
);

const server = http.createServer(function(req, res) {
  res.statusCode = 200;
  res.setHeader('Content-Type', 'text/plain');
  res.end(body);
});

server.listen(0);

server.on('listening', common.mustCall(function() {
  const s = new net.Socket();
  s.connect(this.address().port, this.address().host);
  s.write(`HEAD / HTTP/1.1\r\nHost: ${this.address()}:${this.address().port}\r\n\r\n`);
  s.on('data', function(d) {
    assert(regex.test(d.toString()));
    s.end();
    server.close();
  });
}));
