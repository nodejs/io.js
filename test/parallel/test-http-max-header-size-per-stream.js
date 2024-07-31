'use strict';
const common = require('../common');
const assert = require('assert');
const http = require('http');
const { duplexPair } = require('stream');

// Test that setting the `maxHeaderSize` option works on a per-stream-basis.

// Test 1: The server sends larger headers than what would otherwise be allowed.
{
  const [ clientSide, serverSide ] = duplexPair();

  const req = http.request({
    createConnection: common.mustCall(() => clientSide),
    maxHeaderSize: http.maxHeaderSize * 4
  }, common.mustCall((res) => {
    assert.strictEqual(res.headers.hello, 'A'.repeat(http.maxHeaderSize * 3));
    res.resume();  // We don’t actually care about contents.
    res.on('end', common.mustCall());
  }));
  req.end();

  serverSide.resume();  // Dump the request
  serverSide.end('HTTP/1.1 200 OK\r\n' +
                 'Hello: ' + 'A'.repeat(http.maxHeaderSize * 3) + '\r\n' +
                 'Content-Length: 0\r\n' +
                 '\r\n\r\n');
}

// Test 2: The same as Test 1 except without the option, to make sure it fails.
{
  const [ clientSide, serverSide ] = duplexPair();

  const req = http.request({
    createConnection: common.mustCall(() => clientSide)
  }, common.mustNotCall());
  req.end();
  req.on('error', common.mustCall());

  serverSide.resume();  // Dump the request
  serverSide.end('HTTP/1.1 200 OK\r\n' +
                 'Hello: ' + 'A'.repeat(http.maxHeaderSize * 3) + '\r\n' +
                 'Content-Length: 0\r\n' +
                 '\r\n\r\n');
}

// Test 3: The client sends larger headers than what would otherwise be allowed.
{
  const testData = 'Hello, World!\n';
  const server = http.createServer(
    { maxHeaderSize: http.maxHeaderSize * 4 },
    common.mustCall((req, res) => {
      res.statusCode = 200;
      res.setHeader('Content-Type', 'text/plain');
      res.end(testData);
    }));

  server.on('clientError', common.mustNotCall());

  const [ clientSide, serverSide ] = duplexPair();
  serverSide.server = server;
  server.emit('connection', serverSide);

  clientSide.write('GET / HTTP/1.1\r\n' +
                   'Host: example.com\r\n' +
                   'Hello: ' + 'A'.repeat(http.maxHeaderSize * 3) + '\r\n' +
                   '\r\n\r\n');
}

// Test 4: The same as Test 3 except without the option, to make sure it fails.
{
  const server = http.createServer(common.mustNotCall());

  server.on('clientError', common.mustCall());

  const [ clientSide, serverSide ] = duplexPair();
  serverSide.server = server;
  server.emit('connection', serverSide);

  clientSide.write('GET / HTTP/1.1\r\n' +
                   'Hello: ' + 'A'.repeat(http.maxHeaderSize * 3) + '\r\n' +
                   '\r\n\r\n');
}
