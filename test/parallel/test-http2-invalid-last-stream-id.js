// Flags: --expose-internals
'use strict';

const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');

const h2 = require('http2');
const net = require('net');
const assert = require('assert');
const { ServerHttp2Session } = require('internal/http2/core');

async function sendInvalidLastStreamId(server) {
  const client = new net.Socket();

  const address = server.address();
  if (!common.hasIPv6 && address.family === 'IPv6') {
    // Necessary to pass CI running inside containers.
    client.connect(address.port);
  } else {
    client.connect(address);
  }

  client.on('connect', common.mustCall(function() {
    // HTTP/2 preface
    client.write(Buffer.from('PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n', 'utf8'));

    // Empty SETTINGS frame
    client.write(Buffer.from([0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00]));

    // GOAWAY frame with custom debug message
    const goAwayFrame = [
      0x00, 0x00, 0x21, // Length: 33 bytes
      0x07, // Type: GOAWAY
      0x00, // Flags
      0x00, 0x00, 0x00, 0x00, // Stream ID: 0
      0x00, 0x00, 0x00, 0x01, // Last Stream ID: 1
      0x00, 0x00, 0x00, 0x00, // Error Code: 0 (No error)
    ];

    // Add the debug message
    const debugMessage = 'client transport shutdown';
    const goAwayBuffer = Buffer.concat([
      Buffer.from(goAwayFrame),
      Buffer.from(debugMessage, 'utf8'),
    ]);

    client.write(goAwayBuffer);
    client.destroy();
  }));
}

const server = h2.createServer();

server.on('error', common.mustNotCall());

server.on(
  'sessionError',
  common.mustCall((err, session) => {
    // When destroying the session, on Windows, we would get ECONNRESET
    // errors, make sure we take those into account in our tests.
    if (err.code !== 'ECONNRESET') {
      assert.strictEqual(err.code, 'ERR_HTTP2_ERROR');
      assert.strictEqual(err.name, 'Error');
      assert.strictEqual(err.message, 'Protocol error');
      assert.strictEqual(session instanceof ServerHttp2Session, true);
    }
    session.close();
    server.close();
  }),
);

server.listen(
  0,
  common.mustCall(async () => {
    await sendInvalidLastStreamId(server);
  }),
);
