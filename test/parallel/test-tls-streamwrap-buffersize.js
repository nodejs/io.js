'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const fixtures = require('../common/fixtures');
const { duplexPair } = require('stream');
const tls = require('tls');
const net = require('net');

// This test ensures that `bufferSize` also works for those tlsSockets
// created from `socket` of `Duplex`, with which, TLSSocket will wrap
// sockets in `StreamWrap`.
{
  const iter = 10;

  function createDuplex(port) {
    const [ clientSide, serverSide ] = duplexPair();

    return new Promise((resolve, reject) => {
      const socket = net.connect({
        port,
      }, common.mustCall(() => {
        clientSide.pipe(socket);
        socket.pipe(clientSide);
        clientSide.on('close', common.mustCall(() => socket.destroy()));
        socket.on('close', common.mustCall(() => clientSide.destroy()));

        resolve(serverSide);
      }));
    });
  }

  const server = tls.createServer({
    key: fixtures.readKey('agent2-key.pem'),
    cert: fixtures.readKey('agent2-cert.pem')
  }, common.mustCall((socket) => {
    let str = '';
    socket.setEncoding('utf-8');
    socket.on('data', (chunk) => { str += chunk; });

    socket.on('end', common.mustCall(() => {
      assert.strictEqual(str, 'a'.repeat(iter - 1));
      server.close();
    }));
  }));

  server.listen(0, common.mustCall(() => {
    const { port } = server.address();
    createDuplex(port).then((socket) => {
      const client = tls.connect({
        socket,
        rejectUnauthorized: false,
      }, common.mustCall(() => {
        assert.strictEqual(client.bufferSize, 0);

        for (let i = 1; i < iter; i++) {
          client.write('a');
          assert.strictEqual(client.bufferSize, i);
        }

        client.on('end', common.mustCall());
        client.on('close', common.mustCall(() => {
          assert.strictEqual(client.bufferSize, undefined);
        }));

        client.end();
      }));
    });
  }));
}
