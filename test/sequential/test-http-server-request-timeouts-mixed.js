'use strict';

const common = require('../common');
const assert = require('assert');
const { createServer } = require('http');
const { connect } = require('net');

// This test validates that request are correct checked for both requests and headers timeout in various situations.

const requestBodyPart1 = 'POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 20\r\n';
const requestBodyPart2 = 'Connection: close\r\n\r\n1234567890';
const requestBodyPart3 = '1234567890';

const responseOk = 'HTTP/1.1 200 OK\r\n';
const responseTimeout = 'HTTP/1.1 408 Request Timeout\r\n';

const headersTimeout = common.platformTimeout(2000);
const connectionsCheckingInterval = headersTimeout / 8;

const server = createServer({
  headersTimeout,
  requestTimeout: headersTimeout * 2,
  keepAliveTimeout: 0,
  connectionsCheckingInterval
}, common.mustCall((req, res) => {
  req.resume();

  req.on('end', () => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end();
  });
}, 4));

assert.strictEqual(server.headersTimeout, headersTimeout);
assert.strictEqual(server.requestTimeout, headersTimeout * 2);

let i = 0;
function createClient(server) {
  const request = {
    index: i++,
    client: connect(server.address().port),
    response: '',
    completed: false
  };

  request.client.setEncoding('utf8');
  request.client.on('data', common.mustCallAtLeast((chunk) => {
    request.response += chunk;
  }));

  request.client.on('end', common.mustCall(() => {
    request.completed = true;
  }));

  request.client.on('error', common.mustNotCall());

  request.client.resume();

  return request;
}

server.listen(0, common.mustCall(() => {
  const request1 = createClient(server);
  let request2;
  let request3;
  let request4;
  let request5;

  // Send the first request and stop before the body
  request1.client.write(requestBodyPart1);

  // After a little while send two new requests
  setTimeout(() => {
    request2 = createClient(server);
    request3 = createClient(server);

    // Send the second request, stop in the middle of the headers
    request2.client.write(requestBodyPart1);

    // Send the third request and stop in the middle of the headers
    request3.client.write(requestBodyPart1);
  }, headersTimeout * 0.2);

  // After another little while send the last two new requests
  setTimeout(() => {
    request4 = createClient(server);
    request5 = createClient(server);

    // Send the fourth request, stop in the middle of the headers
    request4.client.write(requestBodyPart1);
    // Send the fifth request, stop in the middle of the headers
    request5.client.write(requestBodyPart1);
  }, headersTimeout * 0.6);

  setTimeout(() => {
    // Finish the first request
    request1.client.write(requestBodyPart2 + requestBodyPart3);

    // Complete headers for all requests but second
    request3.client.write(requestBodyPart2);
    request4.client.write(requestBodyPart2);
    request5.client.write(requestBodyPart2);
  }, headersTimeout * 0.8);

  setTimeout(() => {
    // After the first timeout, the first request should have been completed and second timedout
    assert(request1.completed);
    assert(request2.completed);
    assert(!request3.completed);
    assert(!request4.completed);
    assert(!request5.completed);

    assert(request1.response.startsWith(responseOk));
    assert(request2.response.startsWith(responseTimeout)); // It is expired due to headersTimeout
  }, headersTimeout * 1.4);

  setTimeout(() => {
    // Complete the body for the fourth request
    request4.client.write(requestBodyPart3);
  }, headersTimeout * 1.5);

  setTimeout(() => {
    // All request should be completed now, either with 200 or 408
    assert(request3.completed);
    assert(request4.completed);
    assert(request5.completed);

    assert(request3.response.startsWith(responseTimeout)); // It is expired due to requestTimeout
    assert(request4.response.startsWith(responseOk));
    assert(request5.response.startsWith(responseTimeout)); // It is expired due to requestTimeout
    server.close();
  }, headersTimeout * 3 + connectionsCheckingInterval);
}));
