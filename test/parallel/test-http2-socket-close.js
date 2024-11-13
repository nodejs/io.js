'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const net = require('net');
const h2 = require('http2');

const tlsOptions = {
  key: fixtures.readKey('agent1-key.pem'),
  cert: fixtures.readKey('agent1-cert.pem'),
  ALPNProtocols: ['h2']
};

// Create a net server that upgrades sockets to HTTP/2 manually, handles the
// request, and then shuts down via a short socket timeout and a longer H2 session
// timeout. This is an unconventional way to shut down a session (the underlying
// socket closing first) but it should work - critically, it shouldn't segfault
// (as it did until Node v20.5.1).

let serverRawSocket;
let serverH2Session;

const netServer = net.createServer((socket) => {
  serverRawSocket = socket;
  h2Server.emit('connection', socket);
});

const h2Server = h2.createSecureServer(tlsOptions, (req, res) => {
  res.writeHead(200);
  res.end();
});

h2Server.on('session', (session) => {
  serverH2Session = session;
});

netServer.listen(0, common.mustCall(() => {
  const proxyClient = h2.connect(`https://localhost:${netServer.address().port}`, {
    rejectUnauthorized: false
  });

  proxyClient.on('error', () => {});
  proxyClient.on('close', common.mustCall(() => {
    netServer.close();
  }));

  const req = proxyClient.request({
    ':method': 'GET',
    ':path': '/'
  });

  req.on('error', () => {});
  req.on('response', common.mustCall((response) => {
    assert.strictEqual(response[':status'], 200);

    // Asynchronously shut down the server's connections after the response,
    // but not in the order it typically expects:
    setTimeout(() => {
      serverRawSocket.destroy();

      setTimeout(() => {
        serverH2Session.close();
      }, 10);
    }, 10);
  }));
}));
