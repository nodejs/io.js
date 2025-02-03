'use strict';
const common = require('../common');

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const fixtures = require('../common/fixtures');
const assert = require('assert');
const https = require('https');
const http = require('http');
const tls = require('tls');
const { finished, duplexPair } = require('stream');

const certFixture = {
  key: fixtures.readKey('agent1-key.pem'),
  cert: fixtures.readKey('agent1-cert.pem'),
  ca: fixtures.readKey('ca1-cert.pem'),
};


// Test that setting the `maxHeaderSize` option works on a per-stream-basis.

// Test 1: The server sends larger headers than what would otherwise be allowed.
{
  const [ clientSide, serverSide ] = duplexPair();

  const req = https.request({
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
                 'Host: example.com\r\n' +
                 'Hello: ' + 'A'.repeat(http.maxHeaderSize * 3) + '\r\n' +
                 'Content-Length: 0\r\n' +
                 '\r\n\r\n');
}

// Test 2: The same as Test 1 except without the option, to make sure it fails.
{
  const [ clientSide, serverSide ] = duplexPair();

  const req = https.request({
    createConnection: common.mustCall(() => clientSide)
  }, common.mustNotCall());
  req.end();
  req.on('error', common.mustCall());

  serverSide.resume();  // Dump the request
  serverSide.end('HTTP/1.1 200 OK\r\n' +
                 'Host: example.com\r\n' +
                 'Hello: ' + 'A'.repeat(http.maxHeaderSize * 3) + '\r\n' +
                 'Content-Length: 0\r\n' +
                 '\r\n\r\n');
}

// Test 3: The client sends larger headers than what would otherwise be allowed.
{
  const testData = 'Hello, World!\n';
  const server = https.createServer(
    { maxHeaderSize: http.maxHeaderSize * 4,
      ...certFixture },
    common.mustCall((req, res) => {
      res.statusCode = 200;
      res.setHeader('Content-Type', 'text/plain');
      res.end(testData);
    }));

  server.on('clientError', common.mustNotCall());

  server.listen(0, common.mustCall(() => {
    const client = tls.connect({
      port: server.address().port,
      rejectUnauthorized: false
    });
    client.write(
      'GET / HTTP/1.1\r\n' +
      'Host: example.com\r\n' +
      'Hello: ' + 'A'.repeat(http.maxHeaderSize * 3) + '\r\n' +
      '\r\n\r\n');
    client.end();

    client.on('data', () => {});
    finished(client, common.mustCall(() => {
      server.close();
    }));
  }));
}

// Test 4: The same as Test 3 except without the option, to make sure it fails.
{
  const server = https.createServer({ ...certFixture }, common.mustNotCall());

  // clientError may be emitted multiple times when header is larger than
  // maxHeaderSize.
  server.on('clientError', common.mustCallAtLeast(1));

  server.listen(0, common.mustCall(() => {
    const client = tls.connect({
      port: server.address().port,
      rejectUnauthorized: false
    });
    client.write(
      'GET / HTTP/1.1\r\n' +
      'Host: example.com\r\n' +
      'Hello: ' + 'A'.repeat(http.maxHeaderSize * 3) + '\r\n' +
      '\r\n\r\n');
    client.end();

    client.on('data', () => {});
    finished(client, common.mustCall(() => {
      server.close();
    }));
  }));
}
