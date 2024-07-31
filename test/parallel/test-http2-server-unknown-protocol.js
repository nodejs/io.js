'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');

// This test verifies that when a server receives an unknownProtocol it will
// not leave the socket open if the client does not close it.

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const h2 = require('http2');
const tls = require('tls');

const server = h2.createSecureServer({
  key: fixtures.readKey('rsa_private.pem'),
  cert: fixtures.readKey('rsa_cert.crt'),
  unknownProtocolTimeout: 500,
  allowHalfOpen: true
});

server.on('secureConnection', common.mustCall((socket) => {
  socket.on('close', common.mustCall(() => {
    server.close();
  }));
}));

server.listen(0, function() {
  // If the client does not send an ALPN connection, and the server has not been
  // configured with allowHTTP1, then the server should destroy the socket
  // after unknownProtocolTimeout.
  tls.connect({
    port: server.address().port,
    rejectUnauthorized: false,
  });

  // If the client sends an ALPN extension that does not contain 'h2', the
  // server should send a fatal alert to the client before a secure connection
  // is established at all.
  tls.connect({
    port: server.address().port,
    rejectUnauthorized: false,
    ALPNProtocols: ['bogus']
  }).on('error', common.mustCall((err) => {
    const allowedErrors = ['ECONNRESET', 'ERR_SSL_TLSV1_ALERT_NO_APPLICATION_PROTOCOL'];
    assert.ok(allowedErrors.includes(err.code), `'${err.code}' was not one of ${allowedErrors}.`);
  }));
});
