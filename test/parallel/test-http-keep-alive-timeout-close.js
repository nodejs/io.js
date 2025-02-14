'use strict';
const common = require('../common');
const assert = require('assert');
const { createServer } = require('http');
const { createConnection } = require('net');

const server = createServer();
server.keepAliveTimeout = 1;

server.on('request', (req, res) => {
  if (req.url === '/async') {
    req.on('data', () => {});
    req.on('end', () => {
      res.end('test-body');
      console.log('Server: Sent response asynchronously');
    });
  } else {
    res.end('test-body');
    console.log('Server: Sent response synchronously');
  }
});

server.listen(0, async () => {
  await sendRequests('async', 'get');
  await sendRequests('async', 'post');
  await sendRequests('sync', 'get');
  await sendRequests('sync', 'post'); // keepAliveTimeout does not work here!!

  server.close();
});

async function sendRequests(type, method) {
  console.log(`=== Testing ${type} response for method "${method}" and wait for keep-alive-close`);

  return new Promise((resolve) => {
    const client = createConnection(server.address().port);
    let closeTimeout = null;

    client.on('connect', () => {
      console.log('Client: Connected to server');

      httpRequest();

      closeTimeout = setTimeout(() => {
        console.log('Client: ERROR: HTTP-Connection is still open - closing from client side now');
        closeTimeout = null;
        client.end();

        assert.fail('keepAliveTimeout did not work');
      }, 1000);
    });

    client.on('data', (data) => {
      console.log('Client: Got response data');
    });

    client.on('close', () => {
      if (closeTimeout) {
        console.log('Client: Server closed connection as expected');
        clearTimeout(closeTimeout);
      }
      resolve();
    });

    function httpRequest() {
      const rawRequests = {
        'post': 'POST /' + type + ' HTTP/1.1\r\n' +
          'Connection: keep-alive\r\n' +
          'Content-Type: application/x-www-form-urlencoded\r\n' +
          'Content-Length: 10\r\n' +
          '\r\n' +
          'Test=67890',
        'get': 'GET /' + type + ' HTTP/1.1\r\n' +
          'Connection: keep-alive\r\n' +
          '\r\n'
      };

      console.log('Client: Sending request');
      client.write(rawRequests[method]);
    }
  });
}
