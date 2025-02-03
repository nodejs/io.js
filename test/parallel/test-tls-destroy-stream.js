'use strict';

const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');

const fixtures = require('../common/fixtures');
const { duplexPair } = require('stream');
const net = require('net');
const assert = require('assert');
const tls = require('tls');

tls.DEFAULT_MAX_VERSION = 'TLSv1.3';

// This test ensures that an instance of StreamWrap should emit "end" and
// "close" when the socket on the other side call `destroy()` instead of
// `end()`.
// Refs: https://github.com/nodejs/node/issues/14605
const CONTENT = 'Hello World';
const tlsServer = tls.createServer(
  {
    key: fixtures.readKey('rsa_private.pem'),
    cert: fixtures.readKey('rsa_cert.crt'),
    ca: [fixtures.readKey('rsa_ca.crt')],
  },
  (socket) => {
    socket.on('close', common.mustCall());
    socket.write(CONTENT);
    socket.destroy();

    socket.on('error', (err) => {
      // destroy() is sync, write() is async, whether write completes depends
      // on the protocol, it is not guaranteed by stream API.
      if (err.code === 'ERR_STREAM_DESTROYED')
        return;
      assert.ifError(err);
    });
  },
);

const server = net.createServer((conn) => {
  conn.on('error', common.mustNotCall());
  // Assume that we want to use data to determine what to do with connections.
  conn.once('data', common.mustCall((chunk) => {
    const [ clientSide, serverSide ] = duplexPair();
    serverSide.on('close', common.mustCall(() => {
      conn.destroy();
    }));
    clientSide.pipe(conn);
    conn.pipe(clientSide);

    conn.on('close', common.mustCall(() => {
      clientSide.destroy();
    }));
    clientSide.on('close', common.mustCall(() => {
      conn.destroy();
    }));

    process.nextTick(() => {
      conn.unshift(chunk);
    });

    tlsServer.emit('connection', serverSide);
  }));
});

server.listen(0, () => {
  const port = server.address().port;
  const conn = tls.connect({ port, rejectUnauthorized: false }, () => {
    // Whether the server's write() completed before its destroy() is
    // indeterminate, but if data was written, we should receive it correctly.
    conn.on('data', (data) => {
      assert.strictEqual(data.toString('utf8'), CONTENT);
    });
    conn.on('error', common.mustNotCall());
    conn.on('close', common.mustCall(() => server.close()));
  });
});
