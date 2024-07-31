'use strict';

const common = require('../common');
const assert = require('assert');

const http = require('http');
const net = require('net');

{
  const msg = [
    'POST / HTTP/1.1',
    'Host: 127.0.0.1',
    'Transfer-Encoding: chunked',
    'Transfer-Encoding: chunked-false',
    'Connection: upgrade',
    '',
    '1',
    'A',
    '0',
    '',
    'GET /flag HTTP/1.1',
    'Host: 127.0.0.1',
    '',
    '',
  ].join('\r\n');

  const server = http.createServer(common.mustNotCall((req, res) => {
    res.end();
  }, 1));

  server.listen(0, common.mustSucceed(() => {
    const client = net.connect(server.address().port, 'localhost');

    let response = '';

    // Verify that the server listener is never called

    client.on('data', common.mustCall((chunk) => {
      response += chunk;
    }));

    client.setEncoding('utf8');
    client.on('error', common.mustNotCall());
    client.on('end', common.mustCall(() => {
      assert.strictEqual(
        response,
        'HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n'
      );
      server.close();
    }));
    client.write(msg);
    client.resume();
  }));
}

{
  const msg = [
    'POST / HTTP/1.1',
    'Host: 127.0.0.1',
    'Transfer-Encoding: chunked',
    ' , chunked-false',
    'Connection: upgrade',
    '',
    '1',
    'A',
    '0',
    '',
    'GET /flag HTTP/1.1',
    'Host: 127.0.0.1',
    '',
    '',
  ].join('\r\n');

  const server = http.createServer(common.mustNotCall());

  server.listen(0, common.mustSucceed(() => {
    const client = net.connect(server.address().port, 'localhost');

    client.on('end', common.mustCall(function() {
      server.close();
    }));

    client.write(msg);
    client.resume();
  }));
}
