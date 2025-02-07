'use strict';

const common = require('../common');
const assert = require('assert');
const { createServer } = require('http');
const { connect } = require('net');

// This test validates that the server returns 408
// after server.requestTimeout if the client
// pauses before start sending the request.

let sendDelayedRequestHeaders;
const requestTimeout = common.platformTimeout(2000);
const server = createServer({
  headersTimeout: 0,
  requestTimeout,
  keepAliveTimeout: 0,
  connectionsCheckingInterval: requestTimeout / 4,
}, common.mustNotCall());
server.on('connection', common.mustCall(() => {
  assert.strictEqual(typeof sendDelayedRequestHeaders, 'function');
  sendDelayedRequestHeaders();
}));

assert.strictEqual(server.requestTimeout, requestTimeout);

server.listen(0, common.mustCall(() => {
  const client = connect(server.address().port);
  let response = '';

  client.setEncoding('utf8');
  client.on('data', common.mustCall((chunk) => {
    response += chunk;
  }));

  client.on('error', () => {
    // Ignore errors like 'write EPIPE' that might occur while the request is
    // sent.
  });

  client.on('close', common.mustCall(() => {
    assert.strictEqual(
      response,
      'HTTP/1.1 408 Request Timeout\r\nConnection: close\r\n\r\n'
    );
    server.close();
  }));

  client.resume();

  sendDelayedRequestHeaders = common.mustCall(() => {
    setTimeout(() => {
      client.write(
        'POST / HTTP/1.1\r\n' +
        'Content-Length: 20\r\n' +
        'Connection: close\r\n\r\n' +
        '12345678901234567890\r\n\r\n'
      );
    }, common.platformTimeout(requestTimeout * 2)).unref();
  });
}));
