'use strict';

const common = require('../common');
const assert = require('assert');
const http = require('http');
const net = require('net');

// This test sends an invalid character to a HTTP server and purposely
// does not handle clientError (even if it sets an event handler).
//
// The idea is to let the server emit multiple errors on the socket,
// mostly due to parsing error, and make sure they don't result
// in leaking event listeners.

{
  let i = 0;
  let socket;
  process.on('warning', common.mustNotCall());

  const server = http.createServer(common.mustNotCall());

  server.on('clientError', common.mustCallAtLeast((err) => {
    assert.strictEqual(err.code, 'HPE_INVALID_METHOD');
    assert.strictEqual(err.rawPacket.toString(), '*');

    if (i === 20) {
      socket.end();
    } else {
      socket.write('*');
      i++;
    }
  }, 1));

  server.listen(0, () => {
    socket = net.createConnection({ port: server.address().port });

    socket.on('connect', () => {
      socket.write('*');
    });

    socket.on('close', () => {
      server.close();
    });
  });
}
