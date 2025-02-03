'use strict';
const common = require('../common');
if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const fixtures = require('../common/fixtures');
const assert = require('assert');
const https = require('https');
const tls = require('tls');
const { finished, duplexPair } = require('stream');

const certFixture = {
  key: fixtures.readKey('agent1-key.pem'),
  cert: fixtures.readKey('agent1-cert.pem'),
  ca: fixtures.readKey('ca1-cert.pem'),
};


// Test that setting the `insecureHTTPParse` option works on a per-stream-basis.

// Test 1: The server sends an invalid header.
{
  const [ clientSide, serverSide ] = duplexPair();

  const req = https.request({
    rejectUnauthorized: false,
    createConnection: common.mustCall(() => clientSide),
    insecureHTTPParser: true
  }, common.mustCall((res) => {
    assert.strictEqual(res.headers.hello, 'foo\x08foo');
    res.resume();  // We don’t actually care about contents.
    res.on('end', common.mustCall());
  }));
  req.end();

  serverSide.resume();  // Dump the request
  serverSide.end('HTTP/1.1 200 OK\r\n' +
                 'Host: example.com\r\n' +
                 'Hello: foo\x08foo\r\n' +
                 'Content-Length: 0\r\n' +
                 '\r\n\r\n');
}

// Test 2: The same as Test 1 except without the option, to make sure it fails.
{
  const [ clientSide, serverSide ] = duplexPair();

  const req = https.request({
    rejectUnauthorized: false,
    createConnection: common.mustCall(() => clientSide)
  }, common.mustNotCall());
  req.end();
  req.on('error', common.mustCall());

  serverSide.resume();  // Dump the request
  serverSide.end('HTTP/1.1 200 OK\r\n' +
                 'Host: example.com\r\n' +
                 'Hello: foo\x08foo\r\n' +
                 'Content-Length: 0\r\n' +
                 '\r\n\r\n');
}

// Test 3: The client sends an invalid header.
{
  const testData = 'Hello, World!\n';
  const server = https.createServer(
    { insecureHTTPParser: true,
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
      'Hello: foo\x08foo\r\n' +
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
  const server = https.createServer(
    { ...certFixture },
    common.mustNotCall());

  server.on('clientError', common.mustCall());

  server.listen(0, common.mustCall(() => {
    const client = tls.connect({
      port: server.address().port,
      rejectUnauthorized: false
    });
    client.write(
      'GET / HTTP/1.1\r\n' +
      'Host: example.com\r\n' +
      'Hello: foo\x08foo\r\n' +
      '\r\n\r\n');
    client.end();

    client.on('data', () => {});
    finished(client, common.mustCall(() => {
      server.close();
    }));
  }));
}

// Test 5: Invalid argument type
{
  assert.throws(
    () => https.request({ insecureHTTPParser: 0 }, common.mustNotCall()),
    common.expectsError({
      code: 'ERR_INVALID_ARG_TYPE',
      message: 'The "options.insecureHTTPParser" property must be of' +
      ' type boolean. Received type number (0)'
    })
  );
}
