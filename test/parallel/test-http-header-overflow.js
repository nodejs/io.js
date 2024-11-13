'use strict';
const { expectsError, mustCall } = require('../common');
const assert = require('assert');
const { createServer, maxHeaderSize } = require('http');
const { createConnection } = require('net');

const CRLF = '\r\n';
const DUMMY_HEADER_NAME = 'Cookie: ';
const DUMMY_HEADER_VALUE = 'a'.repeat(
  // Plus one is to make it 1 byte too big
  maxHeaderSize - DUMMY_HEADER_NAME.length + 1
);
const PAYLOAD_GET = 'GET /blah HTTP/1.1';
const PAYLOAD = PAYLOAD_GET + CRLF + DUMMY_HEADER_NAME + DUMMY_HEADER_VALUE;

const server = createServer();

server.on('connection', mustCall((socket) => {
  socket.on('error', expectsError({
    name: 'Error',
    message: 'Parse Error: Header overflow',
    code: 'HPE_HEADER_OVERFLOW',
    bytesParsed: PAYLOAD.length,
    rawPacket: Buffer.from(PAYLOAD)
  }));

  // The data is not sent from the client to ensure that it is received as a
  // single chunk.
  socket.push(PAYLOAD);
}));

server.listen(0, mustCall(() => {
  const c = createConnection(server.address().port);
  let received = '';

  c.on('data', mustCall((data) => {
    received += data.toString();
  }));
  c.on('end', mustCall(() => {
    assert.strictEqual(
      received,
      'HTTP/1.1 431 Request Header Fields Too Large\r\n' +
      'Connection: close\r\n\r\n'
    );
    c.end();
  }));
  c.on('close', mustCall(() => server.close()));
}));
