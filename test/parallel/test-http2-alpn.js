'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');

// This test verifies that http2 server support ALPNCallback option.

if (!common.hasCrypto) common.skip('missing crypto');

const assert = require('assert');
const h2 = require('http2');
const tls = require('tls');

{
  // Server sets two incompatible ALPN options:
  assert.throws(() => h2.createSecureServer({
    ALPNCallback: () => 'a',
    ALPNProtocols: ['b', 'c']
  }), (error) => error.code === 'ERR_TLS_ALPN_CALLBACK_WITH_PROTOCOLS');
}

{
  const server = h2.createSecureServer({
    key: fixtures.readKey('rsa_private.pem'),
    cert: fixtures.readKey('rsa_cert.crt'),
    ALPNCallback: () => 'a',
  });

  server.on(
    'secureConnection',
    common.mustCall((socket) => {
      assert.strictEqual(socket.alpnProtocol, 'a');
      socket.end();
      server.close();
    })
  );

  server.listen(0, function() {
    const client = tls.connect({
      port: server.address().port,
      rejectUnauthorized: false,
      ALPNProtocols: ['a'],
    }, common.mustCall(() => {
      assert.strictEqual(client.alpnProtocol, 'a');
      client.end();
    }));
  });
}
