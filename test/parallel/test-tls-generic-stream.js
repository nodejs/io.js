'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const fixtures = require('../common/fixtures');
const { duplexPair } = require('stream');
const assert = require('assert');
const { TLSSocket, connect } = require('tls');

const key = fixtures.readKey('agent1-key.pem');
const cert = fixtures.readKey('agent1-cert.pem');
const ca = fixtures.readKey('ca1-cert.pem');

const [ clientSide, serverSide ] = duplexPair();

const clientTLS = connect({
  socket: clientSide,
  ca,
  host: 'agent1'  // Hostname from certificate
});
const serverTLS = new TLSSocket(serverSide, {
  isServer: true,
  key,
  cert,
  ca
});

assert.strictEqual(clientTLS.connecting, false);
assert.strictEqual(serverTLS.connecting, false);

clientTLS.on('secureConnect', common.mustCall(() => {
  clientTLS.write('foobar', common.mustCall(() => {
    assert.strictEqual(serverTLS.read().toString(), 'foobar');
    assert.strictEqual(clientTLS._handle.writeQueueSize, 0);
  }));
  assert.ok(clientTLS._handle.writeQueueSize > 0);
}));
