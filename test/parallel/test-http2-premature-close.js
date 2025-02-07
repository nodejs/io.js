// Flags: --expose-internals
'use strict';

const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');

const h2 = require('http2');
const net = require('net');

async function requestAndClose(server) {
  const client = new net.Socket();

  const address = server.address();
  if (!common.hasIPv6 && address.family === 'IPv6') {
    // Necessary to pass CI running inside containers.
    client.connect(address.port);
  } else {
    client.connect(address);
  }

  client.on('connect', common.mustCall(function() {
    // Send HTTP/2 Preface
    client.write(Buffer.from('PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n', 'utf8'));

    // Send a SETTINGS frame (empty payload)
    client.write(Buffer.from([0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00]));

    const streamId = 1;
    // Send a valid HEADERS frame
    const headersFrame = Buffer.concat([
      Buffer.from([
        0x00, 0x00, 0x0c, // Length: 12 bytes
        0x01, // Type: HEADERS
        0x05, // Flags: END_HEADERS + END_STREAM
        (streamId >> 24) & 0xFF, // Stream ID: high byte
        (streamId >> 16) & 0xFF,
        (streamId >> 8) & 0xFF,
        streamId & 0xFF, // Stream ID: low byte
      ]),
      Buffer.from([
        0x82, // Indexed Header Field Representation (Predefined ":method: GET")
        0x84, // Indexed Header Field Representation (Predefined ":path: /")
        0x86, // Indexed Header Field Representation (Predefined ":scheme: http")
        0x44, 0x0a, // Custom ":authority: localhost"
        0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x68, 0x6f, 0x73, 0x74,
      ]),
    ]);
    client.write(headersFrame);

    // Send a valid DATA frame
    const dataFrame = Buffer.concat([
      Buffer.from([
        0x00, 0x00, 0x05, // Length: 5 bytes
        0x00, // Type: DATA
        0x00, // Flags: No flags
        (streamId >> 24) & 0xFF, // Stream ID: high byte
        (streamId >> 16) & 0xFF,
        (streamId >> 8) & 0xFF,
        streamId & 0xFF, // Stream ID: low byte
      ]),
      Buffer.from('Hello', 'utf8'), // Data payload
    ]);
    client.write(dataFrame);

    // Does not wait for server to reply. Shutdown the socket
    client.end();
  }));
}

const server = h2.createServer();

server.on('error', common.mustNotCall());

server.on(
  'session',
  common.mustCall((session) => {
    session.on('close', common.mustCall(() => {
      server.close();
    }));
  }),
);

server.listen(
  0,
  common.mustCall(async () => {
    await requestAndClose(server);
  }),
);
