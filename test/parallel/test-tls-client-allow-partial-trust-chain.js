'use strict';
const common = require('../common');
if (!common.hasCrypto) { common.skip('missing crypto'); };

const assert = require('assert');
const { once } = require('events');
const fixtures = require('../common/fixtures');

// agent6-cert.pem is signed by intermediate cert of ca3.
// The server has a cert chain of agent6->ca3->ca1(root).

const { it, beforeEach, afterEach, describe } = require('node:test');

describe('allowPartialTrustChain', { skip: !common.hasCrypto }, function() {
  const tls = require('tls');
  let server;
  let client;
  let opts;

  beforeEach(async function() {
    server = tls.createServer({
      ca: fixtures.readKey('ca3-cert.pem'),
      key: fixtures.readKey('agent6-key.pem'),
      cert: fixtures.readKey('agent6-cert.pem'),
    }, (socket) => socket.resume());
    server.listen(0);
    await once(server, 'listening');

    opts = {
      port: server.address().port,
      ca: fixtures.readKey('ca3-cert.pem'),
      checkServerIdentity() {}
    };
  });

  afterEach(async function() {
    client?.destroy();
    server?.close();
  });

  it('can connect successfully with allowPartialTrustChain: true', async function() {
    client = tls.connect({ ...opts, allowPartialTrustChain: true });
    await once(client, 'secureConnect'); // Should not throw
  });

  it('fails without with allowPartialTrustChain: true for an intermediate cert in the CA', async function() {
    // Consistency check: Connecting fails without allowPartialTrustChain: true
    await assert.rejects(async () => {
      const client = tls.connect(opts);
      await once(client, 'secureConnect');
    }, { code: 'UNABLE_TO_GET_ISSUER_CERT' });
  });
});
